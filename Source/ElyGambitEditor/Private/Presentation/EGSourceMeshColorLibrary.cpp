#include "Presentation/EGSourceMeshColorLibrary.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

bool UEGSourceMeshColorLibrary::RestoreSourceVertexColors(UStaticMesh* Mesh,const TArray<FVector>& Positions,
 const TArray<FVector2D>& UVs,const TArray<FLinearColor>& Colors,bool bNormalizeTransport,FString& Result)
{
#if WITH_EDITOR
 if(!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Assets/Characters/"))
  || !Mesh->GetPathName().Contains(TEXT("/Effects/Meshes/")) || Positions.IsEmpty()
  || (!UVs.IsEmpty() && Positions.Num()!=UVs.Num()) || Positions.Num()!=Colors.Num()) {Result=TEXT("Invalid owned FX mesh or source attribute counts");return false;}
 for(int32 I=0;I<Positions.Num();++I)
  if(Positions[I].ContainsNaN() || (!UVs.IsEmpty() && UVs[I].ContainsNaN()) || !FMath::IsFinite(Colors[I].R) || !FMath::IsFinite(Colors[I].G) || !FMath::IsFinite(Colors[I].B) || !FMath::IsFinite(Colors[I].A)) {Result=TEXT("Nonfinite source attributes");return false;}
 FMeshDescription* Description=Mesh->GetMeshDescription(0);
 if(!Description) {Result=TEXT("No editable LOD0 mesh description");return false;}
 FStaticMeshAttributes Attributes(*Description);
 const auto VertexPositions=Attributes.GetVertexPositions();const auto InstanceUVs=Attributes.GetVertexInstanceUVs();
 struct FCandidate {TArray<int32> Mapping;double Score=0;int32 Mode=0;bool bValid=true;};
 TArray<FCandidate> Solutions;
 // OBJ/FBX import may reflect Y and reverse V. Select using all actual position
 // and UV correspondences, rejecting conflicting equal-score colour mappings.
 for(int32 Mode=0;Mode<4;++Mode)
 {
  TMap<FIntVector,TArray<int32>> Cells;
  auto Position=[&](int32 I){FVector P=Positions[I];if(Mode&1)P.Y=-P.Y;return P;};
  auto Cell=[](const FVector& P){return FIntVector(FMath::RoundToInt(P.X*100),FMath::RoundToInt(P.Y*100),FMath::RoundToInt(P.Z*100));};
  for(int32 I=0;I<Positions.Num();++I)Cells.FindOrAdd(Cell(Position(I))).Add(I);
  FCandidate Candidate;Candidate.Mode=Mode;
  for(const FVertexInstanceID Instance:Description->VertexInstances().GetElementIDs())
  {
   const FVector P(VertexPositions[Description->GetVertexInstanceVertex(Instance)]);
   const FVector2D UV(InstanceUVs.Get(Instance,0));const FIntVector Key=Cell(P);
   int32 Best=INDEX_NONE;double Error=DBL_MAX;
   for(int32 X=-1;X<=1;++X)for(int32 Y=-1;Y<=1;++Y)for(int32 Z=-1;Z<=1;++Z)
    if(const auto* Indices=Cells.Find(Key+FIntVector(X,Y,Z)))for(int32 I:*Indices)
    {
     const double Distance=FVector::DistSquared(P,Position(I));if(Distance>.0001)continue;
     FVector2D SourceUV=UVs.IsEmpty()?UV:UVs[I];if(!UVs.IsEmpty() && (Mode&2))SourceUV.Y=1-SourceUV.Y;
     const double E=Distance+(UV-SourceUV).SizeSquared();
     if(E<Error){Error=E;Best=I;}
    }
   if(Best==INDEX_NONE || Error>.00011) {Candidate.bValid=false;break;}
   Candidate.Mapping.Add(Best);Candidate.Score+=Error;
  }
  if(Candidate.bValid)Solutions.Add(MoveTemp(Candidate));
 }
 if(Solutions.IsEmpty()){Result=TEXT("No complete source position/UV correspondence; mesh unchanged");return false;}
 Solutions.Sort([](const FCandidate& A,const FCandidate& B){return A.Score<B.Score;});
 const FCandidate& Best=Solutions[0];
 for(int32 S=1;S<Solutions.Num();++S)if(FMath::Abs(Solutions[S].Score-Best.Score)<1.e-10)
  for(int32 I=0;I<Best.Mapping.Num();++I)
   if(!Colors[Best.Mapping[I]].Equals(Colors[Solutions[S].Mapping[I]],.0001f))
    {Result=TEXT("Ambiguous source colour correspondence; mesh unchanged");return false;}
 // Validation above is read-only. Mutate only after every instance maps.
 Mesh->Modify();auto InstanceColors=Attributes.GetVertexInstanceColors();int32 Index=0;
 for(const FVertexInstanceID Instance:Description->VertexInstances().GetElementIDs())
 {
  const FLinearColor& C=Colors[Best.Mapping[Index++]];InstanceColors[Instance]=FVector4f(C.R,C.G,C.B,C.A);
 }
 const bool bReflected=bNormalizeTransport && (Best.Mode&1);
 if(bReflected)
 {
  auto P=Attributes.GetVertexPositions();
  for(const FVertexID Vertex:Description->Vertices().GetElementIDs())P[Vertex].Y=-P[Vertex].Y;
  auto Normals=Attributes.GetVertexInstanceNormals();auto Tangents=Attributes.GetVertexInstanceTangents();
  auto Signs=Attributes.GetVertexInstanceBinormalSigns();
  for(const FVertexInstanceID Instance:Description->VertexInstances().GetElementIDs())
  {Normals[Instance].Y=-Normals[Instance].Y;Tangents[Instance].Y=-Tangents[Instance].Y;Signs[Instance]=-Signs[Instance];}
  Description->ReverseAllPolygonFacing();
 }
 Mesh->CommitMeshDescription(0);Mesh->Build(true);Mesh->MarkPackageDirty();
 Result=FString::Printf(TEXT("Mapped %d vertex instances; Y reflection=%d V reflection=%d; transport corrected=%d; squared error %.9g"),Index,Best.Mode&1,(Best.Mode>>1)&1,bReflected,Best.Score);
 return true;
#else
 Result=TEXT("Source import repair is editor-only");return false;
#endif
}

TArray<FLinearColor> UEGSourceMeshColorLibrary::SourceVertexInstanceColors(UStaticMesh* Mesh)
{
 TArray<FLinearColor> Result;
#if WITH_EDITOR
 if(Mesh)if(const FMeshDescription* Description=Mesh->GetMeshDescription(0))
 {
  const FStaticMeshConstAttributes Attributes(*Description);const auto Colors=Attributes.GetVertexInstanceColors();
  for(const FVertexInstanceID Instance:Description->VertexInstances().GetElementIDs())
  {const FVector4f C=Colors[Instance];Result.Emplace(C.X,C.Y,C.Z,C.W);}
 }
#endif
 return Result;
}

TArray<FVector> UEGSourceMeshColorLibrary::SourceVertexPositions(UStaticMesh* Mesh)
{
 TArray<FVector> Result;
#if WITH_EDITOR
 if(Mesh)if(const FMeshDescription* Description=Mesh->GetMeshDescription(0))
 {
  const FStaticMeshConstAttributes Attributes(*Description);const auto Positions=Attributes.GetVertexPositions();
  for(const FVertexID Vertex:Description->Vertices().GetElementIDs())Result.Emplace(Positions[Vertex]);
 }
#endif
 return Result;
}

