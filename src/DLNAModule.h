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

typedef void(*AddDLNADeviceCallback)(const char* uuid, int uuidLength, const char* title, int titleLength, const char* iconurl, int iconLength, const char* manufacturer, int manufacturerLength);
typedef void(*RemoveDLNADeviceCallback)(const char* uuid, int uuidLength);

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

    const std::filesystem::path logFile;
    UpnpClient_Handle handle;

private:
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

private:
    void RemoveServer(const char* udn);
    void ParseNewServer(IXML_Document* doc, const char* location);
#if _WIN64
    char8_t* GetBestAdapterInterfaceName();
#endif
};

