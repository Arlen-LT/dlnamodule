#pragma once
#include <queue>
#include <list>
#include <mutex>
#include <string>
#include <tuple>
#include <thread>
#include <map>
#include <filesystem>

#include "upnp.h"

typedef void(*BrowseDLNAFolderCallback)(const char* folderxml, int xmlLength, const char* uuid, int uuidLength, const char* objid, int objidLength);
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

struct BrowseDLNAFolderInfo
{
    const char* uuid = nullptr;
    int uuidLength = 0;
    const char* objid = nullptr;
    int objidLength = 0;
    const char* folderXml = nullptr;
    int folderXmlLength = 0;

    BrowseDLNAFolderInfo(const char* uuid, int uuidLength, const char* objid, int objidLength, const char* folderxml, int xmlLength)
    {
        if (uuid != nullptr)
        {
            this->uuidLength = uuidLength;
            this->uuid = new char[uuidLength + 1]();
            memcpy((void*)this->uuid, (void*)uuid, sizeof(char) * uuidLength);
        }

        if (objid != nullptr)
        {
            this->objidLength = objidLength;
            this->objid = new char[objidLength + 1]();
            memcpy((void*)this->objid, (void*)objid, sizeof(char) * objidLength);
        }

        if (folderxml != nullptr)
        {
            this->folderXmlLength = xmlLength;
            this->folderXml = new char[xmlLength + 1]();
            memcpy((void*)this->folderXml, (void*)folderxml, sizeof(char) * xmlLength);
        }
    }

    ~BrowseDLNAFolderInfo()
    {
        if (uuid != nullptr)
        {
            delete[] uuid;
        }

        if (objid != nullptr)
        {
            delete[] objid;
        }

        if (folderXml != nullptr)
        {
            delete[] folderXml;
        }
    }

    void SetXml(const std::string& xt)
    {
        if (folderXml != nullptr)
        {
            delete[] folderXml;
            folderXml = nullptr;
            folderXmlLength = 0;
        }
        folderXmlLength = xt.size();
        folderXml = new char[folderXmlLength + 1]();
        memcpy((void*)folderXml, (void*)(xt.c_str()), folderXmlLength);
    }
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

    UpnpDevice(const std::string& udn)
        : UDN(udn)
        , deviceType(UnknownDevice)
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
    BrowseDLNAFolderCallback ptrToUnityBrowseDLNAFolderCallback = nullptr;

    const std::filesystem::path logFile;
private:
    UpnpClient_Handle handle;
    BrowseDLNAFolderInfo* currentBrowseFolderTask = nullptr;
    std::queue<std::shared_ptr<UpnpDevice>> queueAddDeviceInfo;
    std::queue<std::shared_ptr<UpnpDevice>> queueRemoveDeviceInfo;
    std::queue<std::shared_ptr<BrowseDLNAFolderInfo>> queueBrowseFolderInfo;
    std::mutex deviceQueueMutex, taskQueueMutex, currentTaskMutex;

    std::mutex UpnpDeviceMapMutex;
    std::map<std::string, UpnpDevice> UpnpDeviceMap;

    volatile bool isDLNAModuleRunning = false;
    std::condition_variable cvTaskThread;

#if __cplusplus >= 202002L
    std::atomic_flag discoverAtomicFlag;
#else
    std::atomic_flag discoverAtomicFlag{ false };
#endif

public:
    void StartupModule();
    void ShutdownModule();
    void Refresh();
    void Update();

    void BrowseDLNAFolderByUnity(const char* uuid, int uuidLength, const char* objid, int objidLength);

private:
    void StartDiscover();
    void StopDiscover();
    void TaskThread();

    void AddServer(const std::string& udn, const std::string& friendlyName, const std::string& location, const std::string& iconUrl, const std::string& manufacturer);
    void RemoveServer(const char* udn);
    void ParseNewServer(IXML_Document* doc, const char* location);
    std::string BrowseAction(const char* objectID, const char* flag, const char* filter, const char* startingIndex, const char* requestCount, const char* sortCriteria, const char* controlUrl);
    std::string GetIconURL(IXML_Element* device, const char* baseURL);
    char* iri2uri(const char* iri);
    char* DecodeUri(char* str);
    bool IsUriValidate(const char* str, const char* extras);
    int ParseUrl(URLInfo* url, const char* str);
#if _WIN64
    char8_t* GetBestAdapterInterfaceName();
#endif
};
