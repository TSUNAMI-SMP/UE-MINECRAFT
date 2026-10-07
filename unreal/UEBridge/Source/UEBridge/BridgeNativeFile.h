#pragma once
#include "CoreMinimal.h"

/** Replacement never deletes the destination before the new file is ready. */
namespace BridgeNativeFile {
    UEBRIDGE_API bool Replace(const FString& Destination,const FString& Temporary);
    UEBRIDGE_API bool WriteTextAtomic(const FString& Path,const FString& Text,FString& Error);
}
