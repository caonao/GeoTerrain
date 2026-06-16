#include "Terrain/LandscapeBuilder.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeImportHelper.h"
#include "Editor.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------

// UE5 landscape: height range maps to ±256 landscape units when scale = 1.
// Each landscape unit = ActorScale.Z cm.
// Total height span = 512 * ActorScale.Z cm.
static constexpr float UE_LANDSCAPE_HEIGHT_RANGE = 512.f; // landscape units across 0..65535

// Earth: metres per degree of latitude
static constexpr float METRES_PER_DEG_LAT = 111320.f;

// ---------------------------------------------------------------------------
// Scale helpers
// ---------------------------------------------------------------------------

float FLandscapeBuilder::ComputeXYScale(const FElevationGrid& Grid)
{
    // Real-world distance per grid vertex (cm)
    const float AvgLat  = (Grid.LatMax + Grid.LatMin) * 0.5f;
    const float LonCos  = FMath::Cos(FMath::DegreesToRadians(AvgLat));
    const float LatSpanM = (Grid.LatMax - Grid.LatMin) * METRES_PER_DEG_LAT;
    const float LonSpanM = (Grid.LonMax - Grid.LonMin) * METRES_PER_DEG_LAT * LonCos;
    const float MPerVertexLat = LatSpanM / FMath::Max(Grid.Height - 1, 1);
    const float MPerVertexLon = LonSpanM / FMath::Max(Grid.Width  - 1, 1);
    // Average and convert to cm. UE5 landscape: scale 1 = 1 cm per unit.
    return (MPerVertexLat + MPerVertexLon) * 0.5f * 100.f;
}

float FLandscapeBuilder::ComputeZScale(float ElevMin, float ElevMax)
{
    const float RangeCM = FMath::Max(ElevMax - ElevMin, 1.f) * 100.f;
    // ZScale such that UE_LANDSCAPE_HEIGHT_RANGE * ZScale = RangeCM
    return RangeCM / UE_LANDSCAPE_HEIGHT_RANGE;
}

// ---------------------------------------------------------------------------
// Heightmap conversion
// ---------------------------------------------------------------------------

TArray<uint16> FLandscapeBuilder::ToHeightmap(const TArray<float>& Elevations,
                                               float ElevMin, float ElevMax)
{
    TArray<uint16> Out;
    Out.SetNumUninitialized(Elevations.Num());
    const float Range = FMath::Max(ElevMax - ElevMin, 1.f);

    for (int32 i = 0; i < Elevations.Num(); ++i)
    {
        const float N = (Elevations[i] - ElevMin) / Range; // 0..1
        Out[i] = (uint16)FMath::Clamp(FMath::RoundToInt(N * 65535.f), 0, 65535);
    }
    return Out;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

FLandscapeBuilder::FBuildResult FLandscapeBuilder::Build(const FElevationGrid& Grid)
{
    FBuildResult Result;

    if (!Grid.bValid)
    {
        Result.Error = TEXT("Elevation grid is not valid.");
        return Result;
    }

    // Valid landscape dimension: (n * NumSubsections * SubsectionSizeQuads) + 1
    // Standard: NumSubsections=2, SubsectionSizeQuads=63  → n=8 → 1009
    constexpr int32 NumSubsections      = 2;
    constexpr int32 SubsectionSizeQuads = 63;
    constexpr int32 RequiredSize        = 1009; // 8 * 2 * 63 + 1

    if (Grid.Width != RequiredSize || Grid.Height != RequiredSize)
    {
        Result.Error = FString::Printf(
            TEXT("Grid must be %dx%d for landscape import (got %dx%d). "
                 "Check TargetResolution in FetchBBox()."),
            RequiredSize, RequiredSize, Grid.Width, Grid.Height);
        return Result;
    }

    // --- World ---------------------------------------------------------------
    UWorld* World = (GEditor != nullptr)
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;

    if (!World)
    {
        Result.Error = TEXT("No editor world available. Open a level first.");
        return Result;
    }

    // --- Convert elevations to uint16 heightmap ------------------------------
    TArray<uint16> HeightData = ToHeightmap(Grid.Data, Grid.ElevMin, Grid.ElevMax);

    // --- Scale ---------------------------------------------------------------
    const float XYScale = ComputeXYScale(Grid);
    const float ZScale  = ComputeZScale(Grid.ElevMin, Grid.ElevMax);

    // Actor Z position: place ElevMin at world Z = 0.
    // HeightValue 0 → -256 landscape units from actor origin.
    // With ZScale, -256 * ZScale cm = -RangeCM/2 → actor origin = ElevMin*100 + RangeCM/2
    const float RangeCM   = (Grid.ElevMax - Grid.ElevMin) * 100.f;
    const float ActorZCM  = Grid.ElevMin * 100.f + RangeCM * 0.5f;

    // --- Spawn landscape ------------------------------------------------------
    FActorSpawnParameters Params;
    Params.Name = FName(TEXT("GeoTerrain_Landscape"));

    ALandscape* Landscape = World->SpawnActor<ALandscape>(
        FVector(0.f, 0.f, ActorZCM),
        FRotator::ZeroRotator,
        Params);

    if (!Landscape)
    {
        Result.Error = TEXT("Failed to spawn ALandscape actor.");
        return Result;
    }

    Landscape->SetActorScale3D(FVector(XYScale, XYScale, ZScale));
    Landscape->SetActorLabel(TEXT("GeoTerrain_Landscape"));

    // --- Import heightmap ----------------------------------------------------
    Landscape->Import(
        FGuid::NewGuid(),
        0, 0,
        RequiredSize - 1, RequiredSize - 1,
        NumSubsections,
        SubsectionSizeQuads,
        HeightData.GetData(),
        /*HeightmapFileName=*/nullptr,
        TArray<FLandscapeImportLayerInfo>(),
        ELandscapeImportAlphamapType::Additive
    );

    // Register landscape info so UE knows about it
    ULandscapeInfo* Info = Landscape->CreateLandscapeInfo();
    if (Info)
    {
        Info->UpdateLayerInfoMap(Landscape);
    }

    // Mark level dirty so the user is prompted to save
    World->MarkPackageDirty();

    Result.Landscape  = Landscape;
    Result.XYScaleCM  = XYScale;
    Result.ZScaleCM   = ZScale;
    Result.bSuccess   = true;
    return Result;
}
