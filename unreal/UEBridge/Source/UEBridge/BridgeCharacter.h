#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BridgeCharacter.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ABridgeCharacter();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCameraComponent> BridgeCamera;
};
