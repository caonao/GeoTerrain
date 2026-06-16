#include "Terrain/MaterialApplicator.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeEdit.h"
#include "Math/UnrealMathUtility.h"

// ---------------------------------------------------------------------------
// Slope (degrees) from central differences
// ---------------------------------------------------------------------------

float FMaterialApplicator::ComputeSlopeDeg(const FElevationGrid& Grid,
                                            int32 Row, int32 Col, float CellSizeM)
{
    const float H = Grid.Data[Row * Grid.Width + Col];

    const float L = (Col > 0)              ? Grid.Data[Row * Grid.Width + Col - 1] : H;
    const float R = (Col < Grid.Width - 1) ? Grid.Data[Row * Grid.Width + Col + 1] : H;
    const float U = (Row > 0)              ? Grid.Data[(Row - 1) * Grid.Width + Col] : H;
    const float D = (Row < Grid.Height- 1) ? Grid.Data[(Row + 1) * Grid.Width + Col] : H;

    const float dX = (R - L) / (2.f * CellSizeM);
    const float dY = (D - U) / (2.f * CellSizeM);
    return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(dX * dX + dY * dY)));
}

// ---------------------------------------------------------------------------
// Layer info
// ---------------------------------------------------------------------------

ULandscapeLayerInfoObject* FMaterialApplicator::GetOrCreateLayerInfo(
    ULandscapeInfo* Info, FName LayerName, ALandscape* Landscape)
{
    // Look for an existing layer info with this name
    for (const FLandscapeInfoLayerSettings& LayerSettings : Info->Layers)
    {
        if (LayerSettings.LayerInfoObj && LayerSettings.LayerInfoObj->LayerName == LayerName)
            return LayerSettings.LayerInfoObj;
    }

    // Create a new one inside the landscape's outer package
    ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
        Landscape->GetOutermost(),
        *FString::Printf(TEXT("GeoTerrain_%s_LayerInfo"), *LayerName.ToString()),
        RF_Public | RF_Standalone);

    LayerInfo->LayerName     = LayerName;
    LayerInfo->bNoWeightBlend = false;

    return LayerInfo;
}

// ---------------------------------------------------------------------------
// Main apply function
// ---------------------------------------------------------------------------

FMaterialApplicator::FApplyResult FMaterialApplicator::Apply(ALandscape* Landscape,
                                                              const FElevationGrid& Grid,
                                                              const FMaterialRules& Rules,
                                                              float XYScaleCM)
{
    FApplyResult Result;

    if (!Landscape)
    {
        Result.Error = TEXT("Null landscape.");
        return Result;
    }
    if (!Grid.bValid)
    {
        Result.Error = TEXT("Invalid elevation grid.");
        return Result;
    }

    ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
    if (!Info)
    {
        Result.Error = TEXT("Could not get LandscapeInfo.");
        return Result;
    }

    // Create/find the four layer info objects
    ULandscapeLayerInfoObject* SnowLayer  = GetOrCreateLayerInfo(Info, FName("Snow"),  Landscape);
    ULandscapeLayerInfoObject* RockLayer  = GetOrCreateLayerInfo(Info, FName("Rock"),  Landscape);
    ULandscapeLayerInfoObject* GrassLayer = GetOrCreateLayerInfo(Info, FName("Grass"), Landscape);
    ULandscapeLayerInfoObject* DirtLayer  = GetOrCreateLayerInfo(Info, FName("Dirt"),  Landscape);

    const int32 W        = Grid.Width;
    const int32 H        = Grid.Height;
    const float CellSizeM = XYScaleCM / 100.f;

    // Build per-layer alpha arrays (uint8, 0=none, 255=full)
    TArray<uint8> SnowAlpha, RockAlpha, GrassAlpha, DirtAlpha;
    SnowAlpha.SetNumZeroed(W * H);
    RockAlpha.SetNumZeroed(W * H);
    GrassAlpha.SetNumZeroed(W * H);
    DirtAlpha.SetNumZeroed(W * H);

    for (int32 Row = 0; Row < H; ++Row)
    {
        for (int32 Col = 0; Col < W; ++Col)
        {
            const int32 Idx    = Row * W + Col;
            const float Elev   = Grid.Data[Idx];
            const float Slope  = ComputeSlopeDeg(Grid, Row, Col, CellSizeM);

            if (Elev >= Rules.SnowAltitudeM)
            {
                SnowAlpha[Idx] = 255;
                ++Result.SnowPixels;
            }
            else if (Elev >= Rules.RockAltitudeM || Slope >= Rules.RockSlopeDeg)
            {
                RockAlpha[Idx] = 255;
                ++Result.RockPixels;
            }
            else if (Slope >= Rules.DirtSlopeDeg)
            {
                DirtAlpha[Idx] = 255;
                ++Result.DirtPixels;
            }
            else
            {
                GrassAlpha[Idx] = 255;
                ++Result.GrassPixels;
            }
        }
    }

    // Write to landscape via FLandscapeEditDataInterface
    // Note: requires LandscapeEdit.h from Landscape module
    FLandscapeEditDataInterface EditInterface(Info);

    EditInterface.SetAlphaData(SnowLayer,  0, 0, W - 1, H - 1, SnowAlpha.GetData(),  0);
    EditInterface.SetAlphaData(RockLayer,  0, 0, W - 1, H - 1, RockAlpha.GetData(),  0);
    EditInterface.SetAlphaData(GrassLayer, 0, 0, W - 1, H - 1, GrassAlpha.GetData(), 0);
    EditInterface.SetAlphaData(DirtLayer,  0, 0, W - 1, H - 1, DirtAlpha.GetData(),  0);
    EditInterface.Flush();

    Landscape->MarkPackageDirty();

    Result.bSuccess = true;
    return Result;
}
