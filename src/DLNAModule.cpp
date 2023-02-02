#include <future>
#include <fstream>
#include <sstream>
#include <regex>
#include <variant>

#include "ixml.h"
#include "upnptools.h"
#include "config.h"

#include "base64.h"
#include "logger.h"
#include "DLNAModule.h"
#include "DLNAConfig.h"
#include "URLHandler.h"

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

void DLNAModule::Initialize()
{
    Log(LogLevel::Info, "Starting DLNAModule-%s-%.8s, built at %s", DLNA_VERSION_REF, DLNA_VERSION_COMMIT, BUILD_TIMESTAMP);
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
}

void DLNAModule::Finitialize()
{
    UpnpUnRegisterClient(handle);
    if (UpnpFinish() == UPNP_E_SUCCESS)
        Log(LogLevel::Info, "Upnp SDK finished success");
}

void DLNAModule::Search()
{
    Log(LogLevel::Info, "Searching servers...");
    {
        std::scoped_lock<std::mutex> lock(UpnpDeviceMapMutex);
        UpnpDeviceMap.clear();
    }
    UpnpSearchAsync(handle, MAX_SEARCH_TIME, "ssdp:all", &GetInstance());
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
}

static int UpnpSendActionCallBack(Upnp_EventType eventType, const void* p_event, void* p_cookie)
{
    std::string json, result;
    BrowseDLNAFolderCallback OnBrowseResultCallback;
    auto status = [&]()
    {
        if (eventType != UPNP_CONTROL_ACTION_COMPLETE)
            return -1;

        auto pp_tuple = static_cast<std::tuple<std::string, BrowseDLNAFolderCallback>**>(p_cookie);
        std::tie(json, OnBrowseResultCallback) = **pp_tuple;
        delete (*pp_tuple);

        const UpnpActionComplete* p_result = (UpnpActionComplete*)p_event;
        char* rawHTML = ixmlDocumenttoString(UpnpActionComplete_get_ActionResult(p_result));
        if (rawHTML == nullptr)
            return -3;

        result = ConvertHTMLtoXML(rawHTML);
        ixmlFreeDOMString(rawHTML);

        IXML_Document* parseDoc = ixmlParseBuffer(result.data());
        if (parseDoc == nullptr)
        {
            Log(LogLevel::Error, "Parse result to XML format failed");
            return -4;
        }

        char* tmp = ixmlDocumenttoString(parseDoc);
        if (!tmp)
            return -5;

        result = tmp;
        ixmlFreeDOMString(tmp);

        if (result.empty())
            Log(LogLevel::Error, "Browse failed");
        return 0;
    }();

    using namespace rapidjson;
    rapidjson::Document response(kObjectType);
    auto& allocator = response.GetAllocator();
    response.AddMember("version", Value().SetString("1.0"), allocator);
    response.AddMember("method", "DLNABrowseResponse", allocator);
    response.AddMember("request_body", Value().SetString(json.data(), json.length(), allocator), allocator);
    response.AddMember("results", Value().SetArray().PushBack(Value().SetString(base64_encode(result, false).c_str(), allocator), allocator), allocator);
    response.AddMember("status", status, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    response.Accept(writer);

    if (OnBrowseResultCallback)
        OnBrowseResultCallback(buffer.GetString());
    return status;
}

IXML_Document* DLNAModule::BrowseAction(const char* objectID,
    const char* flag,
    const char* filter,
    const char* startingIndex,
    const char* requestCount,
    const char* sortCriteria,
    const char* controlUrl,
    void* cookie)
{
    IXML_Document* actionDoc = nullptr;
    IXML_Document* browseResultXMLDocument = nullptr;

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

    res = UpnpSendActionAsync(DLNAModule::GetInstance().handle,
        controlUrl,
        CONTENT_DIRECTORY_SERVICE_TYPE,
        nullptr, /* ignored in SDK, must be NULL */
        actionDoc,
        UpnpSendActionCallBack,
        &cookie);

    if (res != UPNP_E_SUCCESS)
    {
        Log(LogLevel::Error, "UpnpSendActionAsync return %s", UpnpGetErrorMessage(res));
        goto browseActionCleanup;
    }

browseActionCleanup:
    ixmlDocument_free(actionDoc);
    return browseResultXMLDocument;
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

#if _WIN32
int vasprintf(char** strp, const char* format, va_list ap)
{
    int len = _vscprintf(format, ap);
    if (len == -1)
        return -1;
    char* str = (char*)malloc((size_t)len + 1);
    if (!str)
        return -1;
    int retval = vsnprintf(str, len + 1, format, ap);
    if (retval == -1) {
        free(str);
        return -1;
    }
    *strp = str;
    return retval;
}

int asprintf(char** strp, const char* fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}
#endif

/*
 * Extracts the result document from a SOAP response
 */
IXML_Document* parseBrowseResult(IXML_Document* p_doc)
{
    assert(p_doc);

    // ixml*_getElementsByTagName will ultimately only case the pointer to a Node
    // pointer, and pass it to a private function. Don't bother have a IXML_Document
    // version of getChildElementValue
    const char* psz_raw_didl = ixmlElement_getFirstChildElementValue((IXML_Element*)p_doc, "Result");

    if (!psz_raw_didl)
        return NULL;

    /* First, try parsing the buffer as is */
    IXML_Document* p_result_doc = ixmlParseBuffer(psz_raw_didl);
    if (!p_result_doc) {
        /* Missing namespaces confuse the ixml parser. This is a very ugly
         * hack but it is needeed until devices start sending valid XML.
         *
         * It works that way:
         *
         * The DIDL document is extracted from the Result tag, then wrapped into
         * a valid XML header and a new root tag which contains missing namespace
         * definitions so the ixml parser understands it.
         *
         * If you know of a better workaround, please oh please fix it */
        const char* psz_xml_result_fmt = "<?xml version=\"1.0\" ?>"
            "<Result xmlns:sec=\"urn:samsung:metadata:2009\">%s</Result>";

        char* psz_xml_result_string = NULL;
        if (-1 == asprintf(&psz_xml_result_string,
            psz_xml_result_fmt,
            psz_raw_didl))
            return NULL;

        p_result_doc = ixmlParseBuffer(psz_xml_result_string);
        free(psz_xml_result_string);
    }

    if (!p_result_doc)
        return NULL;

    IXML_NodeList* p_elems = ixmlDocument_getElementsByTagName(p_result_doc,
        "DIDL-Lite");

    IXML_Node* p_node = ixmlNodeList_item(p_elems, 0);
    ixmlNodeList_free(p_elems);

    return (IXML_Document*)p_node;
}

struct item
{
    const char* objectID,
        * title,
        * psz_artist,
        * psz_genre,
        * psz_album,
        * psz_date,
        * psz_orig_track_nb,
        * psz_album_artist,
        * psz_albumArt;
};

bool TryParseItem(IXML_Element* element)
{
    const char* objectID,
        * title,
        * psz_artist,
        * psz_genre,
        * psz_album,
        * psz_date,
        * psz_orig_track_nb,
        * psz_album_artist,
        * psz_albumArt;

    enum MEDIA_TYPE
    {
        VIDEO = 0,
        AUDIO,
        IMAGE,
        CONTAINER
    };

    MEDIA_TYPE media_type;
    objectID = ixmlElement_getAttribute(element, "id");
    if (!objectID)
        return false;
    title = ixmlElement_getFirstChildElementValue(element, "dc:title");
    if (!title)
        return false;
    const char* psz_subtitles = ixmlElement_getFirstChildElementValue(element, "sec:CaptionInfo");
    if (!psz_subtitles &&
        !(psz_subtitles = ixmlElement_getFirstChildElementValue(element, "sec:CaptionInfoEx")))
        psz_subtitles = ixmlElement_getFirstChildElementValue(element, "pv:subtitlefile");
    psz_artist = ixmlElement_getFirstChildElementValue(element, "upnp:artist");
    psz_genre = ixmlElement_getFirstChildElementValue(element, "upnp:genre");
    psz_album = ixmlElement_getFirstChildElementValue(element, "upnp:album");
    psz_date = ixmlElement_getFirstChildElementValue(element, "dc:date");
    psz_orig_track_nb = ixmlElement_getFirstChildElementValue(element, "upnp:originalTrackNumber");
    psz_album_artist = ixmlElement_getFirstChildElementValue(element, "upnp:albumArtist");
    psz_albumArt = ixmlElement_getFirstChildElementValue(element, "upnp:albumArtURI");
    const char* psz_media_type = ixmlElement_getFirstChildElementValue(element, "upnp:class");
    if (strncmp(psz_media_type, "object.item.videoItem", 21) == 0)
        media_type = VIDEO;
    else if (strncmp(psz_media_type, "object.item.audioItem", 21) == 0)
        media_type = AUDIO;
    else if (strncmp(psz_media_type, "object.item.imageItem", 21) == 0)
        media_type = IMAGE;
    else if (strncmp(psz_media_type, "object.container", 16) == 0)
        media_type = CONTAINER;
    else
        return false;
    return true;
}

std::variant<rapidjson::Value, int> Browse(const std::string& uuid, const std::string& objid, auto& allocator)
{
    std::string location;
    {
        std::lock_guard<std::mutex> lock(DLNAModule::GetInstance().UpnpDeviceMapMutex);
        auto it = DLNAModule::GetInstance().UpnpDeviceMap.find(uuid);
        if (it != DLNAModule::GetInstance().UpnpDeviceMap.end())
        {
            location = it->second.location;
            Log(LogLevel::Info, "BrowseRequest: ObjID=%s, name=%s, location=%s", objid.c_str(), it->second.friendlyName.c_str(), it->second.location.c_str());

        }
    }
    std::string StartingIndex = "0";
    std::string RequestedCount = "5000";
    const char* psz_TotalMatches = "0";
    const char* psz_NumberReturned = "0";
    long  l_reqCount = 0;

    using namespace rapidjson;
    rapidjson::Value resultList(rapidjson::kArrayType);

    do
    {
        IXML_Document* p_response = DLNAModule::GetInstance().BrowseAction(objid.c_str(),
            "BrowseDirectChildren",
            "*",
            StartingIndex.c_str(),
            // Some servers don't understand "0" as "no-limit"
            RequestedCount.c_str(), /* RequestedCount */
            "", /* SortCriteria */
            location.c_str()
        );
        if (!p_response)
        {
            Log(LogLevel::Error, "No response from browse() action");
            return -1;
        }

        psz_TotalMatches = ixmlElement_getFirstChildElementValue((IXML_Element*)p_response, "TotalMatches");
        psz_NumberReturned = ixmlElement_getFirstChildElementValue((IXML_Element*)p_response, "NumberReturned");

        StartingIndex = std::to_string(std::stol(psz_NumberReturned) + std::stol(StartingIndex));
        l_reqCount = std::stol(psz_TotalMatches) - std::stol(StartingIndex);
        RequestedCount = std::to_string(l_reqCount);

        IXML_Document* p_result = parseBrowseResult(p_response);

        ixmlDocument_free(p_response);

        if (!p_result)
        {
            Log(LogLevel::Error, "browse() response parsing failed");
            return -1;
        }

#ifndef NDEBUG
        Log(LogLevel::Info, "Got DIDL document: %s", ixmlPrintDocument(p_result));
#endif

        IXML_NodeList* containerNodeList = ixmlDocument_getElementsByTagName(p_result, "container");

        if (containerNodeList)
        {
            for (unsigned int i = 0; i < ixmlNodeList_length(containerNodeList); i++)
            {
                auto itemElement = (IXML_Element*)ixmlNodeList_item(containerNodeList, i);
                auto tmp = ixmlPrintNode((IXML_Node*)itemElement);
                if (TryParseItem(itemElement))
                    resultList.PushBack(Value().SetObject()
                        .AddMember("isDirectory", true, allocator)
                        .AddMember("xml", Value().SetString(tmp, allocator), allocator)
                        , allocator);
                ixmlFreeDOMString(tmp);
            }
            ixmlNodeList_free(containerNodeList);
        }

        IXML_NodeList* itemNodeList = ixmlDocument_getElementsByTagName(p_result, "item");
        if (itemNodeList)
        {
            for (unsigned int i = 0; i < ixmlNodeList_length(itemNodeList); i++)
            {
                auto itemElement = (IXML_Element*)ixmlNodeList_item(itemNodeList, i);
                auto tmp = ixmlPrintNode((IXML_Node*)itemElement);
                if (TryParseItem(itemElement))
                    resultList.PushBack(Value().SetObject()
                        .AddMember("isDirectory", false, allocator)
                        .AddMember("xml", Value().SetString(tmp, allocator), allocator)
                        , allocator);
                ixmlFreeDOMString(tmp);
            }
            ixmlNodeList_free(itemNodeList);
        }

        ixmlDocument_free(p_result);
    } while (l_reqCount);
    return resultList;
}

bool BrowseFolderByUnity(const char* json, BrowseDLNAFolderCallback2 OnBrowseResultCallback)
{
    if (!json || !OnBrowseResultCallback)
        return false;

    using namespace rapidjson;
    rapidjson::Document request, arguments;
    request.Parse(json);
    if (int errorCode = request.GetParseError())
    {
        Log(LogLevel::Error, "GetParseError：%d", errorCode);
        return false;
    }

    if (!request.HasMember("arguments"))
    {
        Log(LogLevel::Error, "No arguments parsed");
        return false;
    }

    arguments.CopyFrom(request["arguments"], request.GetAllocator());
    arguments.ParseInsitu(const_cast<char*>(request["arguments"].GetString()));

    auto uuid = arguments["uuid"].GetString();
    auto objid = arguments["objid"].GetString();
    if (!uuid || !objid)
    {
        Log(LogLevel::Error, "Broken arguments in browse request");
        return false;
    }

    auto&& server = [](const std::string& uuid)->std::optional<UpnpDevice>
    {
        std::lock_guard<std::mutex> lock(DLNAModule::GetInstance().UpnpDeviceMapMutex);
        auto it = DLNAModule::GetInstance().UpnpDeviceMap.find(uuid);
        if (it != DLNAModule::GetInstance().UpnpDeviceMap.end())
            return it->second;
        return {};
    }(uuid);

    std::visit([&](auto&& var) {
        using T = std::decay_t<decltype(var)>;
    if constexpr (std::is_same_v<T, rapidjson::Value>)
    {
        response.AddMember("results", var, allocator);
        response.AddMember("status", 0, allocator);
    }
    else if constexpr (std::is_same_v<T, int>)
        response.AddMember("status", var, allocator);
        }, Browse(uuid, objid, allocator));

    Log(LogLevel::Info, "BrowseRequest: ObjID=%s, name=%s, location=%s", objid, server->friendlyName.c_str(), server->location.c_str());
    return BrowseAction(objid, "BrowseDirectChildren", "*", "0", "10000", "", server->location.data(), new std::tuple<std::string, BrowseDLNAFolderCallback>(json, OnBrowseResultCallback)) == 0;
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
