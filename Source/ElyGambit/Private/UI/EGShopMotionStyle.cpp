#include "UI/EGShopMotionStyle.h"
#include "UI/EGPearlGoldLayout.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

namespace
{
    // A process-wide cache is not a GC owner; authored widget brushes retain the
    // textures they reference, exactly like FEGVisualStyle.
    TMap<FString,TWeakObjectPtr<UTexture2D>> GCachedTextures;

    FBox2f UvRect(float U0,float V0,float U1,float V1,float Width,float Height)
    {
        return FBox2f(FVector2f(U0/Width,V0/Height),FVector2f(U1/Width,V1/Height));
    }

    FSlateBrush ImageBrush(UTexture2D* Texture,const FBox2f& Uv,FVector2D ImageSize)
    {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.DrawAs=ESlateBrushDrawType::Image;
        Brush.ImageSize=ImageSize;
        Brush.SetUVRegion(Uv);
        Brush.TintColor=FLinearColor::White;
        return Brush;
    }

    struct FSliceAxes
    {
        float Col[4] = {0,0,0,0}; // target x edges
        float Row[4] = {0,0,0,0}; // target y edges
        float SrcCol[4] = {0,0,0,0};
        float SrcRow[4] = {0,0,0,0};
        float TexWidth = 1.f;
        float TexHeight = 1.f;
    };

    FSliceAxes ButtonAxes()
    {
        FSliceAxes A;
        const float Corner=EGPearlGold::ShopPlateCorner;
        const float W=EGPearlGold::ShopCompactButtonWidth;
        const float H=EGPearlGold::ShopCompactButtonHeight;
        A.Col[0]=0.f; A.Col[1]=Corner; A.Col[2]=W-Corner; A.Col[3]=W;
        A.Row[0]=0.f; A.Row[1]=Corner; A.Row[2]=H-Corner; A.Row[3]=H;
        A.SrcCol[0]=EGShopMotion::ButtonPlateU0;
        A.SrcCol[1]=EGShopMotion::ButtonPlateU0+EGShopMotion::ButtonPlateCornerU;
        A.SrcCol[2]=EGShopMotion::ButtonPlateU1-EGShopMotion::ButtonPlateCornerU;
        A.SrcCol[3]=EGShopMotion::ButtonPlateU1;
        A.SrcRow[0]=EGShopMotion::ButtonPlateV0;
        A.SrcRow[1]=EGShopMotion::ButtonPlateV0+EGShopMotion::ButtonPlateCornerV;
        A.SrcRow[2]=EGShopMotion::ButtonPlateV1-EGShopMotion::ButtonPlateCornerV;
        A.SrcRow[3]=EGShopMotion::ButtonPlateV1;
        A.TexWidth=EGShopMotion::ButtonPlateTexWidth;
        A.TexHeight=EGShopMotion::ButtonPlateTexHeight;
        return A;
    }

    FSliceAxes SellAxes()
    {
        FSliceAxes A;
        const float Corner=EGPearlGold::SellPlateRadius;
        const float W=EGPearlGold::SellPlateWidth;
        const float H=EGPearlGold::SellPlateHeight;
        A.Col[0]=0.f; A.Col[1]=Corner; A.Col[2]=W-Corner; A.Col[3]=W;
        A.Row[0]=0.f; A.Row[1]=Corner; A.Row[2]=H-Corner; A.Row[3]=H;
        A.SrcCol[0]=EGShopMotion::SellPlateU0;
        A.SrcCol[1]=EGShopMotion::SellPlateU0+EGShopMotion::SellPlateCornerSource;
        A.SrcCol[2]=EGShopMotion::SellPlateU1-EGShopMotion::SellPlateCornerSource;
        A.SrcCol[3]=EGShopMotion::SellPlateU1;
        A.SrcRow[0]=EGShopMotion::SellPlateV0;
        A.SrcRow[1]=EGShopMotion::SellPlateV0+EGShopMotion::SellPlateCornerSource;
        A.SrcRow[2]=EGShopMotion::SellPlateV1-EGShopMotion::SellPlateCornerSource;
        A.SrcRow[3]=EGShopMotion::SellPlateV1;
        A.TexWidth=EGShopMotion::SellPlateTexWidth;
        A.TexHeight=EGShopMotion::SellPlateTexHeight;
        return A;
    }

    void SliceIndices(EGShopMotion::EPlateSlice Slice,int32& Row,int32& Col)
    {
        const int32 Index=static_cast<int32>(Slice);
        Row=Index/3;
        Col=Index%3;
    }

    FVector2D SliceSize(const FSliceAxes& A,EGShopMotion::EPlateSlice Slice)
    {
        int32 Row,Col;SliceIndices(Slice,Row,Col);
        return FVector2D(A.Col[Col+1]-A.Col[Col],A.Row[Row+1]-A.Row[Row]);
    }

    FVector2D SlicePosition(const FSliceAxes& A,EGShopMotion::EPlateSlice Slice)
    {
        int32 Row,Col;SliceIndices(Slice,Row,Col);
        return FVector2D(A.Col[Col],A.Row[Row]);
    }

    FSlateBrush SliceBrush(const FSliceAxes& A,UTexture2D* Texture,EGShopMotion::EPlateSlice Slice)
    {
        int32 Row,Col;SliceIndices(Slice,Row,Col);
        const FBox2f Uv=UvRect(A.SrcCol[Col],A.SrcRow[Row],A.SrcCol[Col+1],A.SrcRow[Row+1],A.TexWidth,A.TexHeight);
        return ImageBrush(Texture,Uv,SliceSize(A,Slice));
    }

    FVector2D LockSize(bool bLocked)
    {
        // visible_alpha_bbox: LockClosed 313,239-941,1019 (628x780);
        // LockOpen 277,183-977,1076 (700x893). Maintain aspect inside 20x20.
        return bLocked?FVector2D(EGPearlGold::ShopPlateGlyphSize*628.0/780.0,EGPearlGold::ShopPlateGlyphSize)
                      :FVector2D(EGPearlGold::ShopPlateGlyphSize*700.0/893.0,EGPearlGold::ShopPlateGlyphSize);
    }
}

UTexture2D* EGShopMotion::LoadTexture(const TCHAR* AssetName)
{
    if(!AssetName||!*AssetName)return nullptr;
    const FString Key(AssetName);
    if(const auto* Found=GCachedTextures.Find(Key);Found&&Found->IsValid())return Found->Get();
    auto* Texture=LoadObject<UTexture2D>(nullptr,*(FString(ArtFolder)+Key));
    GCachedTextures.Add(Key,Texture);
    return Texture;
}

bool EGShopMotion::AreArtworkAssetsAvailable()
{
    return LoadTexture(ButtonPlateTexture)&&LoadTexture(SellPlateTexture)&&LoadTexture(LockClosedTexture)&&
        LoadTexture(LockOpenTexture)&&LoadTexture(RefreshTexture);
}

FSlateBrush EGShopMotion::ButtonPlateBrush(EPlateSlice Slice)
{
    return SliceBrush(ButtonAxes(),LoadTexture(ButtonPlateTexture),Slice);
}
FVector2D EGShopMotion::ButtonPlateSliceSize(EPlateSlice Slice){return SliceSize(ButtonAxes(),Slice);}
FVector2D EGShopMotion::ButtonPlateSlicePosition(EPlateSlice Slice){return SlicePosition(ButtonAxes(),Slice);}

FSlateBrush EGShopMotion::SellPlateBrush(EPlateSlice Slice)
{
    return SliceBrush(SellAxes(),LoadTexture(SellPlateTexture),Slice);
}
FVector2D EGShopMotion::SellPlateSliceSize(EPlateSlice Slice){return SliceSize(SellAxes(),Slice);}
FVector2D EGShopMotion::SellPlateSlicePosition(EPlateSlice Slice){return SlicePosition(SellAxes(),Slice);}

FVector2D EGShopMotion::GlyphSize(EGlyph Glyph)
{
    switch(Glyph)
    {
    case EGlyph::LockClosed: return LockSize(true);
    case EGlyph::LockOpen:   return LockSize(false);
    case EGlyph::Refresh:    return FVector2D(EGPearlGold::ShopPlateGlyphSize,
        EGPearlGold::ShopPlateGlyphSize*829.0/944.0);
    }
    return FVector2D(EGPearlGold::ShopPlateGlyphSize);
}

const TCHAR* EGShopMotion::GlyphTextureName(EGlyph Glyph)
{
    switch(Glyph)
    {
    case EGlyph::LockClosed: return LockClosedTexture;
    case EGlyph::LockOpen:   return LockOpenTexture;
    case EGlyph::Refresh:    return RefreshTexture;
    }
    return RefreshTexture;
}

FSlateBrush EGShopMotion::GlyphBrush(EGlyph Glyph)
{
    float U0=0,V0=0,U1=1,V1=1,Width=1254.0f,Height=1254.0f;
    switch(Glyph)
    {
    case EGlyph::LockClosed: U0=313;V0=239;U1=941;V1=1019; break;
    case EGlyph::LockOpen:   U0=277;V0=183;U1=977;V1=1076; break;
    case EGlyph::Refresh:    U0=154;V0=218;U1=1098;V1=1047; break;
    }
    return ImageBrush(LoadTexture(GlyphTextureName(Glyph)),
        UvRect(U0,V0,U1,V1,Width,Height),GlyphSize(Glyph));
}

const TCHAR* EGShopMotion::LockClosedImageName(){return TEXT("ShopLockGlyphClosed");}
const TCHAR* EGShopMotion::LockOpenImageName(){return TEXT("ShopLockGlyphOpen");}
const TCHAR* EGShopMotion::RefreshImageName(){return TEXT("ShopRefreshGlyph");}
