#include <future>
#include <fstream>
#include <sstream>
#include <regex>
#include <variant>

#include "ixml.h"
#include "upnptools.h"
#include "config.h"

#include "logger.h"
#include "DLNAModule.h"
#include "DLNAConfig.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#if __ANDROID__
#include <sys/resource.h>
#endif

using namespace std::chrono_literals;

const char* MEDIA_SERVER_DEVICE_TYPE = "urn:schemas-upnp-org:device:MediaServer:1";
const char* CONTENT_DIRECTORY_SERVICE_TYPE = "urn:schemas-upnp-org:service:ContentDirectory:1";
DLNAModule DLNAModule::_dlnaInst;

DLNAModule& DLNAModule::GetInstance()
{
    return _dlnaInst;
}

void DLNAModule::StartupModule()
{
    Log(LogLevel::Info, "Starting DLNAModule-%s-%.8s, built at %s", DLNA_VERSION_REF, DLNA_VERSION_COMMIT, BUILD_TIMESTAMP);
    StartDiscover();
    std::thread(&DLNAModule::TaskThread, &GetInstance()).detach();
}

void DLNAModule::ShutdownModule()
{
    StopDiscover();
    cvTaskThread.notify_all();
}

void DLNAModule::Refresh()
{
    discoverAtomicFlag.clear(std::memory_order_release);
    cvTaskThread.notify_all();
}

void DLNAModule::StartDiscover()
{
    /* Search for media servers */
#if __ANDROID__
    int res = UpnpInit2(nullptr, 0);
#elif _WIN64
    char8_t* bestAdapterName = GetBestAdapterInterfaceName();
    int res = UpnpInit2(reinterpret_cast<char*>(bestAdapterName), 0);
    free(bestAdapterName);
#endif
    if (res != UPNP_E_SUCCESS)
    {
        Log(LogLevel::Error, "Upnp SDK Init error %s", UpnpGetErrorMessage(res));
        return;
    }
    Log(LogLevel::Info, "Upnp SDK init success");
    ixmlRelaxParser(1);

    /* Register a control point */
    res = UpnpRegisterClient(UpnpRegisterClientCallback, &GetInstance(), &handle);
    if (res != UPNP_E_SUCCESS)
    {
        Log(LogLevel::Error, "Upnp control point register failed, return %s", UpnpGetErrorMessage(res));
        return;
    }
    Log(LogLevel::Info, "Upnp control point register success, handle is %d", handle);
    UpnpSetMaxContentLength(INT_MAX);

    if (UpnpSearchAsync(handle, MAX_SEARCH_TIME, MEDIA_SERVER_DEVICE_TYPE, &GetInstance()) != UPNP_E_SUCCESS)
    {
        Log(LogLevel::Error, "Searching server failed");
        return;
    }
    Log(LogLevel::Info, "Searching server success");
    isDLNAModuleRunning = true;
}

void DLNAModule::StopDiscover()
{
    UpnpUnRegisterClient(handle);
    if (UpnpFinish() == UPNP_E_SUCCESS)
        Log(LogLevel::Info, "Upnp SDK finished success");
    isDLNAModuleRunning = false;
}

int DLNAModule::UpnpRegisterClientCallback(Upnp_EventType eventType, const void* event, void* cookie)
{
    switch (eventType)
    {
    case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
    case UPNP_DISCOVERY_SEARCH_RESULT:
    {
        UpnpDiscovery* discoverResult = (UpnpDiscovery*)event;
        IXML_Document* description = nullptr;
        int res = UpnpDownloadXmlDoc(UpnpString_get_String(UpnpDiscovery_get_Location(discoverResult)), &description);
        if (res != UPNP_E_SUCCESS)
            return res;

        GetInstance().ParseNewServer(description, UpnpString_get_String(UpnpDiscovery_get_Location(discoverResult)));
        ixmlDocument_free(description);
    }
    break;

    case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE:
    {
        UpnpDiscovery* discoverResult = (UpnpDiscovery*)event;
        GetInstance().RemoveServer(UpnpString_get_String(UpnpDiscovery_get_DeviceID(discoverResult)));
    }
    break;

    case UPNP_DISCOVERY_SEARCH_TIMEOUT:
    case UPNP_EVENT_RECEIVED:
    case UPNP_EVENT_SUBSCRIBE_COMPLETE:
    case UPNP_EVENT_AUTORENEWAL_FAILED:
    case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
        break;

    default:
        break;
    }

    return UPNP_E_SUCCESS;
}

void DLNAModule::Update()
{
    if (ptrToUnityAddDLNADeviceCallBack)
    {
        std::lock_guard<std::mutex> lock(deviceQueueMutex);
        while (!queueAddDeviceInfo.empty())
        {
            std::shared_ptr<UpnpDevice> infoToTreate = queueAddDeviceInfo.front();
            ptrToUnityAddDLNADeviceCallBack(infoToTreate->UDN.data(), infoToTreate->UDN.length(), infoToTreate->friendlyName.data(), infoToTreate->friendlyName.length(), infoToTreate->iconUrl.data(), infoToTreate->iconUrl.length(), infoToTreate->manufacturer.data(), infoToTreate->manufacturer.length());
            queueAddDeviceInfo.pop();
        }
    }

    if (ptrToUnityRemoveDLNADeviceCallback)
    {
        std::lock_guard<std::mutex> lock(deviceQueueMutex);
        while (!queueRemoveDeviceInfo.empty())
        {
            std::shared_ptr<UpnpDevice> infoToTreate = queueRemoveDeviceInfo.front();
            ptrToUnityRemoveDLNADeviceCallback(infoToTreate->UDN.data(), infoToTreate->UDN.length());
            queueRemoveDeviceInfo.pop();
        }
    }

    if (ptrToUnityBrowseDLNAFolderCallback)
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex);
        while (!queueBrowseFolderInfo.empty())
        {
            std::shared_ptr<BrowseDLNAFolderInfo> infoToTreate = queueBrowseFolderInfo.front();
            ptrToUnityBrowseDLNAFolderCallback(infoToTreate->folderXml, infoToTreate->folderXmlLength, infoToTreate->uuid, infoToTreate->uuidLength, infoToTreate->objid, infoToTreate->objidLength);
            queueBrowseFolderInfo.pop();
        }
    }
}

void DLNAModule::TaskThread()
{
    std::mutex taskMutex;
    std::unique_lock<std::mutex> taskThreadLock(taskMutex);
    while (isDLNAModuleRunning)
    {
        if (!discoverAtomicFlag.test_and_set(std::memory_order_acquire))
        {
            Log(LogLevel::Info, "Searching servers...");
            {
                std::scoped_lock<std::mutex> lock(UpnpDeviceMapMutex);
                UpnpDeviceMap.clear();
            }
            UpnpSearchAsync(handle, MAX_SEARCH_TIME, "ssdp:all", &GetInstance());
        }

        BrowseDLNAFolderInfo* pTask = nullptr;
        {
            std::lock_guard<std::mutex> lock(currentTaskMutex);
            std::swap(currentBrowseFolderTask, pTask);
        }

        if (pTask != nullptr)
        {
            std::lock_guard<std::mutex> lock(UpnpDeviceMapMutex);
            auto it = UpnpDeviceMap.find(pTask->uuid);
            if (it != UpnpDeviceMap.end())
            {
                Log(LogLevel::Info, "BrowseRequest: ObjID=%s, name=%s, location=%s", pTask->objid, it->second.friendlyName.c_str(), it->second.location.c_str());
                std::string res = BrowseAction(pTask->objid, "BrowseDirectChildren", "*", "0", "10000", "", it->second.location.data());

                res = std::regex_replace(res, std::regex{ "&amp;" }, "&");
                res = std::regex_replace(res, std::regex{ "&quot;" }, "\"");
                res = std::regex_replace(res, std::regex{ "&gt;" }, ">");
                res = std::regex_replace(res, std::regex{ "&lt;" }, "<");
                res = std::regex_replace(res, std::regex{ "&apos;" }, "'");
                res = std::regex_replace(res, std::regex{ "<unknown>" }, "unknown");

                if (it->second.manufacturer != "Microsoft Corporation")
                {
                    res = std::regex_replace(res, std::regex{ R"((pv:subtitleFileType=")([^"]*)(")|(pv:subtitleFileUri=")([^"]*)("))" }, "");
                    res = std::regex_replace(res, std::regex{ "\xc3\x97" }, "x");
                }

                IXML_Document* parseDoc = ixmlParseBuffer(res.data());
                if (parseDoc == nullptr)
                {
                    Log(LogLevel::Error, "Parse result to XML format failed");
                    res.clear();
                }
                else
                {
                    char* tmp = ixmlDocumenttoString(parseDoc);
                    res = tmp;
                    ixmlFreeDOMString(tmp);
                }

                if (res.empty())
                    Log(LogLevel::Error, "Browse failed");
                pTask->SetXml(res);

                std::lock_guard<std::mutex> taskQueueLock(taskQueueMutex);
                queueBrowseFolderInfo.emplace(std::make_shared<BrowseDLNAFolderInfo>(pTask->uuid, pTask->uuidLength, pTask->objid, pTask->objidLength, pTask->folderXml, pTask->folderXmlLength));
            }
        }

        cvTaskThread.wait(taskThreadLock,
            [this]
            {
#if __cplusplus >= 202002L
                return !isDLNAModuleRunning
                || !discoverAtomicFlag.test()
                || currentBrowseFolderTask;
#else
                bool flag = discoverAtomicFlag.test_and_set(std::memory_order_acquire);
            if (flag == false)
                discoverAtomicFlag.clear(std::memory_order_release);
            return !isDLNAModuleRunning
                || !flag
                || currentBrowseFolderTask;
#endif
            });
    }
}

std::string DLNAModule::BrowseAction(const char* objectID,
    const char* flag,
    const char* filter,
    const char* startingIndex,
    const char* requestCount,
    const char* sortCriteria,
    const char* controlUrl)
{
    IXML_Document* actionDoc = nullptr;
    IXML_Document* browseResultXMLDocument = nullptr;
    char* rawXML = nullptr;
    std::string browseResultString;

    int res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "ObjectID", objectID);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "BrowseFlag", flag);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "Filter", filter);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "StartingIndex", startingIndex);
    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "RequestedCount", requestCount);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "SortCriteria", sortCriteria);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpSendAction(handle,
        controlUrl,
        CONTENT_DIRECTORY_SERVICE_TYPE,
        nullptr, /* ignored in SDK, must be NULL */
        actionDoc,
        &browseResultXMLDocument);

    if (res || !browseResultXMLDocument)
    {
        Log(LogLevel::Error, "UpnpSendAction return %s", UpnpGetErrorMessage(res));
        goto browseActionCleanup;
    }

    rawXML = ixmlDocumenttoString(browseResultXMLDocument);
    if (rawXML != nullptr)
    {
        browseResultString = rawXML;
        ixmlFreeDOMString(rawXML);
    }

browseActionCleanup:
    if (browseResultXMLDocument)
        ixmlDocument_free(browseResultXMLDocument);

    ixmlDocument_free(actionDoc);
    return browseResultString;
}


void DLNAModule::BrowseDLNAFolderByUnity(const char* uuid, int uuidLength, const char* objid, int objidLength)
{
    currentTaskMutex.lock();
    if (currentBrowseFolderTask)
    {
        delete currentBrowseFolderTask;
    }
    currentBrowseFolderTask = new BrowseDLNAFolderInfo(uuid, uuidLength, objid, objidLength, nullptr, 0);
    currentTaskMutex.unlock();

    cvTaskThread.notify_all();
}
#include "base64.h"
std::variant<int, std::string> Browse(const std::string& uuid, const std::string& objid)
{
    std::lock_guard<std::mutex> lock(DLNAModule::GetInstance().UpnpDeviceMapMutex);
    auto it = DLNAModule::GetInstance().UpnpDeviceMap.find(uuid);
    if (it != DLNAModule::GetInstance().UpnpDeviceMap.end())
    {
        Log(LogLevel::Info, "BrowseRequest: ObjID=%s, name=%s, location=%s", objid.c_str(), it->second.friendlyName.c_str(), it->second.location.c_str());
        std::string res = DLNAModule::GetInstance().BrowseAction(objid.c_str(), "BrowseDirectChildren", "*", "0", "10000", "", it->second.location.data());

        res = std::regex_replace(res, std::regex{ "&amp;" }, "&");
        res = std::regex_replace(res, std::regex{ "&quot;" }, "\"");
        res = std::regex_replace(res, std::regex{ "&gt;" }, ">");
        res = std::regex_replace(res, std::regex{ "&lt;" }, "<");
        res = std::regex_replace(res, std::regex{ "&apos;" }, "'");
        res = std::regex_replace(res, std::regex{ "<unknown>" }, "unknown");

        if (it->second.manufacturer != "Microsoft Corporation")
        {
            res = std::regex_replace(res, std::regex{ R"((pv:subtitleFileType=")([^"]*)(")|(pv:subtitleFileUri=")([^"]*)("))" }, "");
            res = std::regex_replace(res, std::regex{ "\xc3\x97" }, "x");
        }

        IXML_Document* parseDoc = ixmlParseBuffer(res.data());
        if (parseDoc == nullptr)
        {
            Log(LogLevel::Error, "Parse result to XML format failed");
            return -1;
        }
        else
        {
            char* tmp = ixmlDocumenttoString(parseDoc);
            res = tmp;
            ixmlFreeDOMString(tmp);
        }

        return base64_encode(res, false);
    }
    return -1;
}

bool BrowseFolderByUnity(const char* json, BrowseDLNAFolderCallback2 OnBrowseResultCallback)
{
    using namespace rapidjson;
    rapidjson::Document request, arguments;
    request.Parse(json);
    if (request.GetParseError())
    {
        Log(LogLevel::Error, "GetParseError：%d", request.GetParseError());
        return false;
    }

    if (!request.HasMember("arguments"))
    {
        Log(LogLevel::Error, "No arguments parsed");
        return false;
    }

    arguments.CopyFrom(request["arguments"], request.GetAllocator());
    arguments.ParseInsitu(const_cast<char*>(request["arguments"].GetString()));

    CHECK_VARIABLE(arguments["needSave"].GetBool(), "%d");

    std::string uuid = arguments["uuid"].GetString() ? arguments["uuid"].GetString() : "";
    std::string objid = arguments["objid"].GetString() ? arguments["objid"].GetString() : "";
    if (uuid.empty() || objid.empty())
    {
        Log(LogLevel::Error, "Broken arguments in browse request");
        return false;
    }

    rapidjson::Document response(kObjectType);
    auto& allocator = response.GetAllocator();
    response.AddMember("version", Value().SetString("1.0"), allocator);
    response.AddMember("method", Value("DLNABrowseResponse"), allocator);
    response.AddMember("request_body", Value().SetString(json, strlen(json), allocator), allocator);

    std::visit([&](auto&& var) {
        using T = std::decay_t<decltype(var)>;
        if constexpr (std::is_same_v<T, std::string>)
        {
            response.AddMember("results", Value().SetArray().PushBack(Value().SetString(var.c_str(), var.length(), allocator), allocator), allocator);
            response.AddMember("status", 0, allocator);
        }
        else if constexpr (std::is_same_v<T, int>)
            response.AddMember("status", var, allocator);
    }, Browse(uuid, objid));

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    response.Accept(writer);
    Log(LogLevel::Info, "BrowseFolderByJson succeed");
    OnBrowseResultCallback(buffer.GetString());
    return true;
}

void DLNAModule::RemoveServer(const char* udn)
{
    if (!udn)
        return;

    {
        std::lock_guard<std::mutex> lock(UpnpDeviceMapMutex);
        auto it = UpnpDeviceMap.find(udn);
        if (it != UpnpDeviceMap.end())
            UpnpDeviceMap.erase(it);
    }

    std::lock_guard<std::mutex> lock(deviceQueueMutex);
    DLNAModule::GetInstance().queueRemoveDeviceInfo.emplace(std::make_shared<UpnpDevice>(udn));
}

void DLNAModule::ParseNewServer(IXML_Document* doc, const char* location)
{
    if (!doc || !location)
        return;

    const char* baseURL = location;
    /* Try to extract baseURL */
    IXML_NodeList* urlList = ixmlDocument_getElementsByTagName(doc, "URLBase");
    if (urlList)
    {
        if (IXML_Node* urlNode = ixmlNodeList_item(urlList, 0))
        {
            IXML_Node* firstUrlNode = ixmlNode_getFirstChild(urlNode);
            if (firstUrlNode)
                baseURL = ixmlNode_getNodeValue(firstUrlNode);
        }
        ixmlNodeList_free(urlList);
    }

    /* Get devices */
    IXML_NodeList* deviceList = ixmlDocument_getElementsByTagName(doc, "device");
    if (!deviceList)
        return;

    for (unsigned int i = 0; i < ixmlNodeList_length(deviceList); i++)
    {
        IXML_Element* device = (IXML_Element*)ixmlNodeList_item(deviceList, i);
        if (!device)
            continue;

        const char* deviceType = ixmlElement_getFirstChildElementValue(device, "deviceType");
        if (!deviceType)
        {
            continue;
        }

        const char* udn = ixmlElement_getFirstChildElementValue(device, "UDN");
        {
            std::lock_guard<std::mutex> lock(UpnpDeviceMapMutex);
            if (!udn || UpnpDeviceMap.find(udn) != UpnpDeviceMap.end())
                continue;
        }

        const char* friendlyName = ixmlElement_getFirstChildElementValue(device, "friendlyName");
        if (!friendlyName)
        {
            continue;
        }

        const char* manufacturer = ixmlElement_getFirstChildElementValue(device, "manufacturer");
        std::string manufacturerString = manufacturer ? manufacturer : "";
        std::string iconUrl = GetIconURL(device, baseURL);

        {
            std::lock_guard<std::mutex> lock(UpnpDeviceMapMutex);
            if (UpnpDeviceMap.find(udn) == UpnpDeviceMap.end())
            {
                UpnpDeviceMap.emplace(std::piecewise_construct, std::forward_as_tuple(udn),
                    std::forward_as_tuple(udn, friendlyName, location, iconUrl, manufacturerString));
                Log(LogLevel::Info, "Device found: DeviceType=%s, UDN=%s, Name=%s", deviceType, udn, friendlyName);
            }
        }

        /* Check for ContentDirectory service. */
        IXML_NodeList* serviceList = ixmlElement_getElementsByTagName(device, "service");
        if (!serviceList)
            continue;

        for (unsigned int j = 0; j < ixmlNodeList_length(serviceList); j++)
        {
            IXML_Element* service = (IXML_Element*)ixmlNodeList_item(serviceList, j);

            const char* serviceType = ixmlElement_getFirstChildElementValue(service, "serviceType");
            if (!serviceType || strncmp(CONTENT_DIRECTORY_SERVICE_TYPE, serviceType, strlen(CONTENT_DIRECTORY_SERVICE_TYPE) - 1))
            {
                continue;
            }

            const char* controlURL = ixmlElement_getFirstChildElementValue(service, "controlURL");
            if (!controlURL)
            {
                continue;
            }

            /* Try to browse content directory. */
            Log(LogLevel::Info, "%s support service:%s, BaseURL=%s, ControlURL=%s", friendlyName, serviceType, baseURL, controlURL);
            std::lock_guard<std::mutex> lock(UpnpDeviceMapMutex);
            auto itr = UpnpDeviceMap.find(udn);
            if (itr != UpnpDeviceMap.end())
                itr->second.deviceType = UpnpDevice::DeviceType::MediaServer;

            char* url = (char*)malloc(strlen(baseURL) + strlen(controlURL) + 1);
            if (!url)
                continue;

            int ret = UpnpResolveURL(baseURL, controlURL, url);
            if (ret == UPNP_E_SUCCESS)
            {
                Log(LogLevel::Info, "UpnpResolveURL success, add device %s", friendlyName);
                itr->second.location = url;
                std::lock_guard<std::mutex> deviceQueueLock(deviceQueueMutex);
                queueAddDeviceInfo.emplace(std::make_shared<UpnpDevice>(itr->second));
            }
            else Log(LogLevel::Error, "UpnpResolveURL return %d, error: %d", ret, errno);
            free(url);
        }
        ixmlNodeList_free(serviceList);
    }
    ixmlNodeList_free(deviceList);
}

std::string DLNAModule::GetIconURL(IXML_Element* device, const char* baseURL)
{
    std::string res;
    URLInfo url;
    IXML_NodeList* iconLists = nullptr;
    IXML_Element* iconList = nullptr;

    if (ParseUrl(&url, baseURL) < 0)
        goto end;

    iconLists = ixmlElement_getElementsByTagName(device, "iconList");
    iconList = (IXML_Element*)ixmlNodeList_item(iconLists, 0);
    ixmlNodeList_free(iconLists);
    if (iconList != nullptr)
    {
        IXML_NodeList* icons = ixmlElement_getElementsByTagName(iconList, "icon");
        if (icons != nullptr)
        {
            unsigned int maxWidth = 0;
            unsigned int maxHeight = 0;
            for (unsigned int i = 0; i < ixmlNodeList_length(icons); ++i)
            {
                IXML_Element* icon = (IXML_Element*)ixmlNodeList_item(icons, i);
                const char* widthStr = ixmlElement_getFirstChildElementValue(icon, "width");
                const char* heightStr = ixmlElement_getFirstChildElementValue(icon, "height");
                if (widthStr == nullptr || heightStr == nullptr)
                    continue;
                unsigned int width = atoi(widthStr);
                unsigned int height = atoi(heightStr);
                if (width <= maxWidth || height <= maxHeight)
                    continue;
                const char* iconUrl = ixmlElement_getFirstChildElementValue(icon, "url");
                if (iconUrl == nullptr)
                    continue;
                maxWidth = width;
                maxHeight = height;
                res = iconUrl;
            }
            ixmlNodeList_free(icons);
        }
    }

    if (!res.empty())
    {
        std::ostringstream oss;
        oss << url.protocol << "://" << url.host << ":" << url.port << res;
        res = oss.str();
    }

end:
    if (url.host != nullptr)
        free(url.host);
    if (url.buffer != nullptr)
        free(url.buffer);
    return res;
}

char* DLNAModule::iri2uri(const char* iri)
{
    const char urihex[] = "0123456789ABCDEF";
    size_t a = 0, u = 0;

    size_t i = 0;
    for (i = 0; iri[i] != '\0'; i++)
    {
        unsigned char c = iri[i];

        if (c < 128)
            a++;
        else
            u++;
    }

    if ((a + u) > (SIZE_MAX / 4))
    {
        errno = ENOMEM;
        return NULL;
    }

    char* uri = (char*)calloc(a + 3 * u + 1, sizeof(char));
    for (char* p = uri; *iri != '\0'; iri++)
    {
        unsigned char c = *iri;

        if (c < 128)
            *(p++) = c;
        else
        {
            *(p++) = '%';
            *(p++) = urihex[c >> 4];
            *(p++) = urihex[c & 0xf];
        }
    }
    return uri;
}

char* DLNAModule::DecodeUri(char* str)
{
    char* in = str, * out = str;
    if (in == NULL)
        return NULL;

    char c;
    while ((c = *(in++)) != '\0')
    {
        if (c == '%')
        {
            char hex[3];

            if (!(hex[0] = *(in++)) || !(hex[1] = *(in++)))
                return NULL;
            hex[2] = '\0';
            *(out++) = strtoul(hex, NULL, 0x10);
        }
        else
            *(out++) = c;
    }
    *out = '\0';
    return str;
}

bool DLNAModule::IsUriValidate(const char* str, const char* extras)
{
    if (!str)
        return false;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        unsigned char c = str[i];
        /* These are the _unreserved_ URI characters (RFC3986 ��2.3) */
        if (std::isalpha(c)
            || std::isdigit(c)
            || strchr("-._~!$&'()*+,;=", c) != NULL
            || strchr(extras, c) != NULL)
        {
            continue;
        }

        if (c == '%'
            && std::isxdigit(static_cast<unsigned char>(str[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(str[i + 2])))
        {
            i += 2;
            continue;
        }
        return false;
    }
    return true;
}

int DLNAModule::ParseUrl(URLInfo* url, const char* str)
{
    if (str == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    int ret = 0;

    char* uri = iri2uri(str);
    if (uri == NULL)
        return -1;
    url->buffer = uri;

    /* URI scheme */
    char* cur = uri;
    char* next = uri;
    while ((*next >= 'A' && *next <= 'Z') || (*next >= 'a' && *next <= 'z')
        || (*next >= '0' && *next <= '9') || memchr("+-.", *next, 3) != NULL)
        next++;

    if (*next == ':')
    {
        *(next++) = '\0';
        url->protocol = cur;
        cur = next;
    }

    /* Fragment */
    next = strchr(cur, '#');
    if (next != NULL)
    {
#if 0  /* TODO */
        * (next++) = '\0';
        url->psz_fragment = next;
#else
        * next = '\0';
#endif
    }

    /* Query parameters */
    next = strchr(cur, '?');
    if (next != NULL)
    {
        *(next++) = '\0';
        url->option = next;
    }

    /* Authority */
    if (strncmp(cur, "//", 2) == 0)
    {
        cur += 2;

        /* Path */
        next = strchr(cur, '/');
        if (next != NULL)
        {
            *next = '\0'; /* temporary nul, reset to slash later */
            url->path = next;
        }
        /*else
            url->psz_path = "/";*/

            /* User name */
        next = strrchr(cur, '@');
        if (next != NULL)
        {
            *(next++) = '\0';
            url->username = cur;
            cur = next;

            /* Password (obsolete) */
            next = strchr(url->username, ':');
            if (next != NULL)
            {
                *(next++) = '\0';
                url->password = next;
                DecodeUri(url->password);
            }
            DecodeUri(url->username);
        }

        /* Host name */
        if (*cur == '[' && (next = strrchr(cur, ']')) != NULL)
        {   /* Try IPv6 numeral within brackets */
            *(next++) = '\0';
            url->host = strdup(cur + 1);

            if (*next == ':')
                next++;
            else
                next = NULL;
        }
        else
        {
            next = strchr(cur, ':');
            if (next != NULL)
                *(next++) = '\0';

            const char* host = DecodeUri(cur);
            for (const char* p = host; *p; p++)
            {
                if (((unsigned char)*p) >= 0x80)
                {
                    errno = ENOSYS;
                    ret = -1;
                    break;
                }
            }

            if (ret != -1)
                url->host = strdup(host);
        }

        if (url->host != NULL && !IsUriValidate(url->host, ":"))
        {
            free(url->host);
            url->host = NULL;
            errno = EINVAL;
            ret = -1;
        }

        /* Port number */
        if (next != NULL && *next)
        {
            char* end;
            unsigned long port = strtoul(next, &end, 10);

            if (strchr("0123456789", *next) == NULL || *end || port > UINT_MAX)
            {
                errno = EINVAL;
                ret = -1;
            }

            url->port = port;
        }

        if (url->path != NULL)
            *url->path = '/'; /* restore leading slash */
    }
    else
    {
        url->path = cur;
    }

    if (url->path != NULL && !IsUriValidate(url->path, "/@:"))
    {
        url->path = NULL;
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}

#if _WIN64
char8_t* DLNAModule::GetBestAdapterInterfaceName()
{
    wchar_t psz_uri[32];
    ULONG size = 32;
    ULONG adapts_size = 0;
    PIP_ADAPTER_ADDRESSES adapts_item;
    PIP_ADAPTER_ADDRESSES adapts = nullptr;
    PIP_ADAPTER_UNICAST_ADDRESS p_best_ip = nullptr;
    PIP_ADAPTER_ADDRESSES bestAdapter = nullptr;

    /* Get Adapters addresses required size. */
    int ret = GetAdaptersAddresses(AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
        NULL,
        adapts,
        &adapts_size);
    if (ret != ERROR_BUFFER_OVERFLOW)
    {
        Log(LogLevel::Error, "GetAdaptersAddresses failed to find list of adapters");
        return NULL;
    }

    /* Allocate enough memory. */
    adapts = (PIP_ADAPTER_ADDRESSES)malloc(adapts_size);
    ret = GetAdaptersAddresses(AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
        NULL,
        adapts,
        &adapts_size);
    if (ret != 0)
    {
        Log(LogLevel::Error, "GetAdaptersAddresses failed to find list of adapters");
        return NULL;
    }

    /* find one with multicast capabilities */
    for (adapts_item = adapts; adapts_item != NULL; adapts_item = adapts_item->Next)
    {
        if (adapts_item->Flags & IP_ADAPTER_NO_MULTICAST ||
            adapts_item->OperStatus != IfOperStatusUp)
            continue;

        /* make sure it supports 239.255.255.250 */
        for (PIP_ADAPTER_MULTICAST_ADDRESS p_multicast = adapts_item->FirstMulticastAddress;
            p_multicast != NULL;
            p_multicast = p_multicast->Next)
        {
            if (((struct sockaddr_in*)p_multicast->Address.lpSockaddr)->sin_addr.S_un.S_addr == inet_addr("239.255.255.250"))
            {
                /* get an IPv4 address */
                for (PIP_ADAPTER_UNICAST_ADDRESS p_unicast = adapts_item->FirstUnicastAddress;
                    p_unicast != NULL;
                    p_unicast = p_unicast->Next)
                {
                    //size = sizeof(psz_uri) / sizeof(wchar_t);
                    //if (WSAAddressToString(p_unicast->Address.lpSockaddr,
                    //    p_unicast->Address.iSockaddrLength,
                    //    NULL, psz_uri, &size) == 0)
                    if (p_best_ip == NULL || p_best_ip->ValidLifetime > p_unicast->ValidLifetime)
                    {
                        p_best_ip = p_unicast;
                        bestAdapter = adapts_item;
                    }
                }
                break;
            }
        }
    }

    if (p_best_ip != NULL)
        goto done;

    /* find any with IPv4 */
    for (adapts_item = adapts; adapts_item != NULL; adapts_item = adapts_item->Next)
    {
        if (adapts_item->Flags & IP_ADAPTER_NO_MULTICAST ||
            adapts_item->OperStatus != IfOperStatusUp)
            continue;

        for (PIP_ADAPTER_UNICAST_ADDRESS p_unicast = adapts_item->FirstUnicastAddress;
            p_unicast != NULL;
            p_unicast = p_unicast->Next)
        {
            if (p_best_ip == NULL || p_best_ip->ValidLifetime > p_unicast->ValidLifetime)
            {
                p_best_ip = p_unicast;
                bestAdapter = adapts_item;
            }
        }
    }

done:
    if (p_best_ip != NULL)
    {
        size = sizeof(psz_uri) / sizeof(wchar_t);
        WSAAddressToString(p_best_ip->Address.lpSockaddr,
            p_best_ip->Address.iSockaddrLength,
            NULL, psz_uri, &size);
        char tmpIp[32] = { 0 };
        wcstombs(tmpIp, psz_uri, size);

        char8_t* tmpIfName = (char8_t*)calloc(LINE_SIZE, sizeof(char8_t));
        int ret = WideCharToMultiByte(CP_UTF8, 0, bestAdapter->FriendlyName, -1, NULL, 0, NULL, NULL);
        ret = WideCharToMultiByte(CP_UTF8, 0, bestAdapter->FriendlyName, ret, reinterpret_cast<char*>(tmpIfName), ret, NULL, NULL);
        Log(LogLevel::Info, "Get the best ip is %s, ifname is %s", tmpIp, reinterpret_cast<char*>(tmpIfName));
        free(adapts);
        return tmpIfName;
    }
    free(adapts);
    return NULL;
}
#endif
