#pragma once
#include "CoreMinimal.h"
#include "Presentation/EGChessAnimInstance.h"
class USkeletalMeshComponent;class UEGHeroDefinition;
/** One presentation boundary for normal AnimGraphs and the existing sequence mixer. */
struct ELYGAMBIT_API FEGAnimationPlayback
{
 static bool Configure(USkeletalMeshComponent* Mesh,const UEGHeroDefinition* Definition);
 static bool Play(USkeletalMeshComponent* Mesh,EGPresentationAction Action,float Rate=1.f);
 static void Stop(USkeletalMeshComponent* Mesh);
 static void Speed(USkeletalMeshComponent* Mesh,float Value);
 static void Rate(USkeletalMeshComponent* Mesh,float Value);
 static bool IsActive(USkeletalMeshComponent* Mesh);
 static bool IsSupported(USkeletalMeshComponent* Mesh);
};
