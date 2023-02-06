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
