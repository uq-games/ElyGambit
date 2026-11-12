#include "Diagnostics/EGResourceAuditLibrary.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIStats.h"
#include "DynamicRHI.h"
#include "UObject/UObjectIterator.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dxgi1_4.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

FString UEGResourceAuditLibrary::TextureSourceId(UTexture2D* Texture)
{
#if WITH_EDITORONLY_DATA
 return Texture?Texture->Source.GetId().ToString():FString();
#else
 return FString();
#endif
}
bool UEGResourceAuditLibrary::PrepareCardCompression(UTexture2D* Texture)
{
#if WITH_EDITOR
 if(!Texture||!Texture->GetPathName().StartsWith(TEXT("/Game/Assets/Cards/")))return false;
 Texture->Modify();Texture->CompressionSettings=TC_BC7;Texture->CompressionNone=false;
 Texture->DeferCompression=false;Texture->PostEditChange();return true;
#else
 return false;
#endif
}

FString UEGResourceAuditLibrary::CaptureMemorySample(const FString& Label,bool bWriteTextureInventory)
{
 FString Folder;FParse::Value(FCommandLine::Get(),TEXT("EGResourceOutput="),Folder);
 if(Folder.IsEmpty())Folder=FPaths::ProjectSavedDir()/TEXT("ResourceAudit");
 IFileManager::Get().MakeDirectory(*Folder,true);
 int64 Used=-1,Budget=-1;
#if PLATFORM_WINDOWS
 // DXGI reports this process's local residency and its current OS budget, not total GPU usage.
 IDXGIFactory1* Factory=nullptr;
 if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))))
 {
  for(UINT Index=0;;++Index)
  {
   IDXGIAdapter1* Adapter=nullptr;
   if(Factory->EnumAdapters1(Index,&Adapter)==DXGI_ERROR_NOT_FOUND)break;
   if(!Adapter)break;
   DXGI_ADAPTER_DESC1 Desc{};Adapter->GetDesc1(&Desc);
   if(Desc.VendorId==GRHIVendorId&&Desc.DeviceId==GRHIDeviceId)
   {
    IDXGIAdapter3* Adapter3=nullptr;
    if(SUCCEEDED(Adapter->QueryInterface(IID_PPV_ARGS(&Adapter3))))
    {
     DXGI_QUERY_VIDEO_MEMORY_INFO Info{};
     if(SUCCEEDED(Adapter3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&Info)))
     {Used=static_cast<int64>(Info.CurrentUsage);Budget=static_cast<int64>(Info.Budget);}
     Adapter3->Release();
    }
    Adapter->Release();break;
   }
   Adapter->Release();
  }
  Factory->Release();
 }
#endif
 FTextureMemoryStats Stats;if(GDynamicRHI)RHIGetTextureMemoryStats(Stats);
 const FString CSVPath=Folder/TEXT("memory.csv");
 if(!IFileManager::Get().FileExists(*CSVPath))
  FFileHelper::SaveStringToFile(TEXT("seconds,label,frame_ms,process_local_used_bytes,os_local_budget_bytes,streaming_texture_bytes,nonstreaming_texture_bytes,texture_pool_bytes,process_ram_bytes\n"),*CSVPath);
 FString SafeLabel=Label;SafeLabel.ReplaceInline(TEXT(","),TEXT("_"));SafeLabel.ReplaceInline(TEXT("\n"),TEXT("_"));
 const FString Row=FString::Printf(TEXT("%.3f,%s,%.4f,%lld,%lld,%llu,%llu,%lld,%llu\n"),
  FPlatformTime::Seconds(),*SafeLabel,FApp::GetDeltaTime()*1000.,Used,Budget,Stats.StreamingMemorySize,
  Stats.NonStreamingMemorySize,Stats.TexturePoolSize,FPlatformMemory::GetStats().UsedPhysical);
 FFileHelper::SaveStringToFile(Row,*CSVPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
 if(bWriteTextureInventory)
 {
  struct FEntry{uint64 Bytes;FString Row;};TArray<FEntry> Entries;
  for(TObjectIterator<UTexture2D> It;It;++It)
  {
   auto* T=*It;if(T->HasAnyFlags(RF_ClassDefaultObject)||!T->GetResource())continue;
   const uint64 Bytes=T->CalcTextureMemorySizeEnum(TMC_ResidentMips);
   const bool bSpecial=T->LODGroup==TEXTUREGROUP_UI||T->GetPathName().StartsWith(TEXT("/Game/Assets/UI/"))||T->CompressionSettings==TC_Masks||T->Filter==TF_Nearest;
   const FString Flag=T->GetSizeX()>2048||T->GetSizeY()>2048?TEXT("large"):
    (T->NeverStream&&!bSpecial?TEXT("review_nonstreaming"):
     (T->GetNumMips()<=1&&!bSpecial&&FMath::Max(T->GetSizeX(),T->GetSizeY())>=512?TEXT("review_no_mips"):TEXT("")));
   Entries.Add({Bytes,FString::Printf(TEXT("%llu,%d,%d,%d,%d,%d,%d,%s,%s\n"),Bytes,T->GetSizeX(),T->GetSizeY(),T->GetNumMips(),
    T->NeverStream?1:0,static_cast<int32>(T->LODGroup),bSpecial?1:0,*Flag,*T->GetPathName())});
  }
  Entries.Sort([](const FEntry& A,const FEntry& B){return A.Bytes>B.Bytes;});
  FString Inventory=TEXT("resident_gpu_bytes,width,height,mips,never_stream,texture_group,special_sampling_exception,review,path\n");
  for(const auto& Entry:Entries)Inventory+=Entry.Row;
  FFileHelper::SaveStringToFile(Inventory,*(Folder/(FPaths::MakeValidFileName(SafeLabel)+TEXT("-textures.csv"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
 return CSVPath;
}
