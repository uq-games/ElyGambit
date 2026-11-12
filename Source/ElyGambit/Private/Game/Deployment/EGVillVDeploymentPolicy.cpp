#include "Game/Deployment/EGVillVDeploymentPolicy.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"

void FEGVillVDeploymentPolicy::PlayEntrances(AEGCardManager& Manager)
{
 if(!Manager.CanEditPlacement()||!Manager.GetWorld()||Manager.GetWorld()->bIsTearingDown||!Manager.Roster.IsValid())return;
 for(const auto& Entry:Manager.DevicePieces)if(auto* Part=Entry.Value.Get();Part&&!Part->IsHidden()&&Manager.PendingDeviceEntrances.Contains(Entry.Key))
 {
  FEGHeroInstance H;if(!Manager.Roster->GetHero(Part->RosterId,H)||!H.Definition)continue;
  const auto* D=Cast<UEGVillVSkillDefinition>(H.Definition->SkillDefinition);const int32 Index=Part->PartRole==EEGDeploymentPart::Gatling?0:1;
  if(!D||!D->DeviceSpawns.IsValidIndex(Index))continue;
  const auto& SpawnRef=D->DeviceSpawns[Index];
  if(auto* Spawn=SpawnRef.Get())
  {Part->PlayPreparationEntrance(Spawn);Manager.PendingDeviceEntrances.Remove(Entry.Key);continue;}
  const FSoftObjectPath Path=SpawnRef.ToSoftObjectPath();
  if(!Path.IsValid()||Manager.LoadingDeviceEntrances.Contains(Path))continue;
  Manager.LoadingDeviceEntrances.Add(Path);
  const TWeakObjectPtr<AEGCardManager> WeakManager(&Manager);
  UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,FStreamableDelegate::CreateLambda([WeakManager,Path]()
  {
   if(auto* M=WeakManager.Get();IsValid(M)&&!M->bEnding)
   {
    M->LoadingDeviceEntrances.Remove(Path);
    if(Path.ResolveObject())M->PlayPendingDeviceEntrances();
    else UE_LOG(LogTemp,Warning,TEXT("VillV preparation entrance unavailable: %s"),*Path.ToString());
   }
  }));
 }
}

bool FEGVillVDeploymentPolicy::FindCells(const AEGCardManager& Manager,FIntPoint Person,FIntPoint& Gatling,FIntPoint& Cannon)
{
 const auto* Board=Manager.Board.Get();if(!Board||!Board->IsPlayerCell(Person))return false;
 TArray<FIntPoint> Free;
 for(int32 Y=0;Y<Board->PlayerRows;++Y)for(int32 X=0;X<Board->Columns;++X)
 {const FIntPoint Cell(X,Y);if(Cell!=Person&&Board->IsCellAvailable(Cell))Free.Add(Cell);}
 Free.Sort([Person](FIntPoint A,FIntPoint B)
 {const int32 DA=(A-Person).SizeSquared(),DB=(B-Person).SizeSquared();return DA!=DB?DA<DB:A.Y!=B.Y?A.Y<B.Y:A.X<B.X;});
 if(Free.Num()<2)return false;Gatling=Free[0];Cannon=Free[1];return true;
}

bool FEGVillVDeploymentPolicy::EnsureLayout(AEGCardManager& Manager,AEGDeployedPiece* Main)
{
 FEGHeroInstance H;if(!IsValid(Main)||!Manager.Roster.IsValid()||!Manager.Roster->GetHero(Main->RosterId,H)||!H.Definition||H.Definition->HeroKind!=EEGHeroKind::VillV)return false;
 int32 Existing=0;for(const auto& Entry:Manager.DevicePieces)if(Entry.Value.IsValid()&&Entry.Value->RosterId==H.InstanceId)++Existing;
 if(Existing==2)return true;
 if(!H.bVillVLayout)
 {
  if(!FindCells(Manager,Main->Cell,H.GatlingCell,H.CannonCell)||!Manager.Roster->SetVillVLayout(H.InstanceId,Main->Cell,H.GatlingCell,H.CannonCell))return false;
 }
 return SpawnParts(Manager,Main);
}

bool FEGVillVDeploymentPolicy::SpawnParts(AEGCardManager& Manager,AEGDeployedPiece* Main)
{
 auto* Board=Manager.Board.Get();auto* World=Manager.GetWorld();
 FEGHeroInstance H;if(!IsValid(Main)||!Board||!World||!Manager.Roster.IsValid()||!Manager.Roster->GetHero(Main->RosterId,H)||!H.bVillVLayout||!H.Definition)return false;
 const auto* D=Cast<UEGVillVSkillDefinition>(H.Definition->SkillDefinition);if(!D)return false;
 UEGHeroDefinition* Definitions[2]={D->GatlingDefinition.LoadSynchronous(),D->CannonDefinition.LoadSynchronous()};
 const FIntPoint Cells[2]={H.GatlingCell,H.CannonCell};
 if(!Definitions[0]||!Definitions[1]||!Board->IsPlayerCell(Cells[0])||!Board->IsPlayerCell(Cells[1])||!Board->IsCellAvailable(Cells[0])||!Board->IsCellAvailable(Cells[1]))return false;
 TArray<AEGDeployedPiece*> Created;
 auto Rollback=[&](){for(auto* P:Created)if(IsValid(P)){Board->ReleaseCell(P->Cell,P);P->Destroy();}};
 for(int32 I=0;I<2;++I)
 {
  const FTransform Transform(Board->GetActorRotation(),Board->CellToWorld(Cells[I]));
  auto* Part=World->SpawnActorDeferred<AEGDeployedPiece>(AEGDeployedPiece::StaticClass(),Transform,&Manager,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!IsValid(Part)||Part->IsActorBeingDestroyed()){Rollback();return false;}
  Part->PartRole=I==0?EEGDeploymentPart::Gatling:EEGDeploymentPart::Cannon;Part->RosterId=H.InstanceId;Part->CardDefinition=Main->CardDefinition;
  Part->HeroVisualDefinition=Definitions[I];Part->AssignPlacement(Board,Cells[I],FGuid::NewGuid());Part->FinishSpawning(Transform);
  if(!IsValid(Part)||Part->IsActorBeingDestroyed()||!Board->TryOccupyCell(Cells[I],Part))
  {if(IsValid(Part))Part->Destroy();Rollback();return false;}
  Part->ApplyHeroDefinition(Definitions[I]);Part->RefreshPreparationVitals();Part->SetPreparationVitalsVisible(false);Created.Add(Part);
 }
 for(auto* Part:Created){Manager.DevicePieces.Add(Part->InstanceId,Part);Part->OnDestroyed.AddDynamic(&Manager,&AEGCardManager::PieceDestroyed);Manager.PendingDeviceEntrances.Add(Part->InstanceId);}
 PlayEntrances(Manager);return true;
}

void FEGVillVDeploymentPolicy::DestroyGroup(AEGCardManager& Manager,FGuid HeroId)
{
 const auto All=Manager.GetDeploymentParts();
 for(auto* P:All)if(P->RosterId==HeroId)
 {
  P->OnDestroyed.RemoveDynamic(&Manager,&AEGCardManager::PieceDestroyed);Manager.Pieces.Remove(P->InstanceId);Manager.DevicePieces.Remove(P->InstanceId);Manager.PendingDeviceEntrances.Remove(P->InstanceId);
  if(Manager.Board)Manager.Board->ReleaseCell(P->Cell,P);P->Destroy();
 }
}

void FEGVillVDeploymentPolicy::SetVisible(AEGCardManager& Manager,FGuid HeroId,bool bVisible)
{
 for(const auto& Entry:Manager.DevicePieces)if(Entry.Value.IsValid()&&Entry.Value->RosterId==HeroId)
 {
  auto* Part=Entry.Value.Get();const bool WasHidden=Part->IsHidden();
  if(!bVisible){Part->SetBattlePresentation(true);Manager.PendingDeviceEntrances.Add(Entry.Key);}
  else if(WasHidden)Part->SetBattlePresentation(false);
  Part->SetActorHiddenInGame(!bVisible);Part->SetActorEnableCollision(bVisible);
 }
 if(bVisible)PlayEntrances(Manager);
}
