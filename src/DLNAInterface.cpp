#include "DLNAModule.h"

#if __ANDROID__
#define DLNA_EXPORT
#elif _WIN64
#define DLNA_EXPORT __declspec(dllexport)
#endif

extern "C" DLNA_EXPORT void SKYBOXStartupDLNA()
{
    DLNAModule::GetInstance().StartupModule();
}

extern "C" DLNA_EXPORT void SKYBOXShutdownDLNA()
{
    DLNAModule::GetInstance().ShutdownModule();
}

extern "C" DLNA_EXPORT void SKYBOXRefreshDLNA()
{
    DLNAModule::GetInstance().Refresh();
}

extern "C" DLNA_EXPORT void SKYBOXDLNAUpdate()
{
    DLNAModule::GetInstance().Update();
}

extern "C" DLNA_EXPORT void BrowseDLNAFolder2(const char* json, BrowseDLNAFolderCallback OnBrowseResultCallback)
{
    BrowseFolderByUnity(json, OnBrowseResultCallback);
}

extern "C" DLNA_EXPORT void SetAddDLNADeviceCallback(AddDLNADeviceCallback OnAddDLNADevice)
{
    DLNAModule::GetInstance().ptrToUnityAddDLNADeviceCallBack = OnAddDLNADevice;
}

extern "C" DLNA_EXPORT void SetRemoveDLNADeviceCallback(RemoveDLNADeviceCallback OnRemoveDLNADevice)
{
    DLNAModule::GetInstance().ptrToUnityRemoveDLNADeviceCallback = OnRemoveDLNADevice;
}

extern "C" DLNA_EXPORT void InitDLNALogFilePath(const char* DLNALogFilePath)
{
    const_cast<std::filesystem::path&>(DLNAModule::GetInstance().logFile) = std::filesystem::u8path(DLNALogFilePath);
}