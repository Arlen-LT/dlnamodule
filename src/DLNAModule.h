#pragma once
#include <queue>
#include <list>
#include <mutex>
#include <string>
#include <tuple>
#include <thread>
#include <map>
#include <any>
#include <filesystem>

#include "upnp.h"

typedef void(*BrowseDLNAFolderCallback)(const char* json);
typedef void(*AddDLNADeviceCallback)(const char* uuid, int uuidLength, const char* title, int titleLength, const char* iconurl, int iconLength, const char* manufacturer, int manufacturerLength);
typedef void(*RemoveDLNADeviceCallback)(const char* uuid, int uuidLength);

struct URLInfo
{
    char* protocol = nullptr;
    char* username = nullptr;
    char* password = nullptr;
    char* host = nullptr;
    unsigned int port = 0;
    char* path = nullptr;
    char* option = nullptr;
    char* buffer = nullptr; /* to be freed */
};

struct UpnpDevice
{
    std::string UDN;
    std::string friendlyName;
    std::string location;
    std::string iconUrl;
    std::string manufacturer;
    enum DeviceType
    {
        UnknownDevice = 0,
        MediaServer = 1,
        MediaRenderer = 2,
    }deviceType;

    UpnpDevice(const UpnpDevice& other)
        : UDN(other.UDN)
        , friendlyName(other.friendlyName)
        , location(other.location)
        , iconUrl(other.iconUrl)
        , manufacturer(other.manufacturer)
        , deviceType(other.deviceType)
    {
    }

    UpnpDevice(const std::string& udn, const std::string& friendlyName, const std::string& location, const std::string& iconUrl, const std::string& manufacturer)
        : UDN(udn)
        , friendlyName(friendlyName)
        , location(location)
        , iconUrl(iconUrl)
        , manufacturer(manufacturer)
        , deviceType(UnknownDevice)
    {
    }
};

class DLNAModule
{
public:
    static DLNAModule& GetInstance();

private:
    static DLNAModule _dlnaInst;
    static int UpnpRegisterClientCallback(Upnp_EventType event_type, const void* p_event, void* p_cookie);

public:
    AddDLNADeviceCallback ptrToUnityAddDLNADeviceCallBack = nullptr;
    RemoveDLNADeviceCallback ptrToUnityRemoveDLNADeviceCallback = nullptr;

    const std::filesystem::path logFile;
private:
    UpnpClient_Handle handle;
    std::queue<std::shared_ptr<UpnpDevice>> queueAddDeviceInfo;
    std::queue<std::shared_ptr<UpnpDevice>> queueRemoveDeviceInfo;
    std::mutex deviceQueueMutex;

public:
    std::mutex UpnpDeviceMapMutex;
    std::map<std::string, UpnpDevice> UpnpDeviceMap;
    std::atomic_flag discoverAtomicFlag;

public:
    void Initialize();
    void Finitialize();
    void Search();
    void Update();
    int BrowseAction(const char* objectID, const char* flag, const char* filter, const char* startingIndex, const char* requestCount, const char* sortCriteria, const char* controlUrl, void* cookie);

private:
    void RemoveServer(const char* udn);
    void ParseNewServer(IXML_Document* doc, const char* location);
    std::string ReplaceAll(const char* src, int srcLen, const char* old_value, const char* new_value);
    std::string ConvertHTMLtoXML(const char* src); 
    std::string GetIconURL(IXML_Element* device, const char* baseURL);
    char* iri2uri(const char* iri);
    char* DecodeUri(char* str);
    bool IsUriValidate(const char* str, const char* extras);
    int ParseUrl(URLInfo* url, const char* str);
#if _WIN64
    char8_t* GetBestAdapterInterfaceName();
#endif
};

static int UpnpSendActionCallBack(Upnp_EventType eventType, const void* p_event, void* p_cookie);
bool BrowseFolderByUnity(const char* json, BrowseDLNAFolderCallback OnBrowseResultCallback);
