#include "BridgeNativeFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <cstdio>
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

bool BridgeNativeFile::Replace(const FString& Destination,const FString& Temporary) {
    // Same directory guarantees a same-volume atomic rename/replacement.
    const FString Dest=FPaths::ConvertRelativePathToFull(Destination),Temp=FPaths::ConvertRelativePathToFull(Temporary);
    if(FPaths::GetPath(Dest)!=FPaths::GetPath(Temp)) return false;
#if PLATFORM_WINDOWS
    return ::MoveFileExW(*Temp,*Dest,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return std::rename(TCHAR_TO_UTF8(*Temp),TCHAR_TO_UTF8(*Dest))==0;
#endif
}
bool BridgeNativeFile::WriteTextAtomic(const FString& Path,const FString& Text,FString& Error) {
    auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
    if(!Platform.CreateDirectoryTree(*FPaths::GetPath(Path))) {Error=TEXT("save_directory_unavailable");return false;}
    const FString Temp=Path+TEXT(".tmp-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TUniquePtr<IFileHandle> File(Platform.OpenWrite(*Temp,false,false));
    if(!File) {Error=TEXT("save_temporary_file_unavailable");return false;}
    FTCHARToUTF8 Bytes(*Text);const bool Written=File->Write(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length()) && File->Flush(true);File.Reset();
    if(!Written || !Replace(Path,Temp)) {Platform.DeleteFile(*Temp);Error=TEXT("save_commit_failed_previous_file_preserved");return false;}
    Error.Empty();return true;
}
