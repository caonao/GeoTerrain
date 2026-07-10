#include "Terrain/LandscapeBuilder.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeImportHelper.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"

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

FLandscapeBuilder::FBuildResult FLandscapeBuilder::Build(const FElevationGrid& Grid,
                                                          const FString& MaterialPath)
{
    FBuildResult Result;

    if (!Grid.bValid)
    {
        Result.Error = TEXT("Elevation grid is not valid.");
        return Result;
    }

    // Valid landscape dimension: (n * NumSubsections * SubsectionSizeQuads) + 1
    // Standard: NumSubsections=2, SubsectionSizeQuads=63 → n=8 → 1009, n=16 → 2017.
    constexpr int32 NumSubsections      = 2;
    constexpr int32 SubsectionSizeQuads = 63;
    constexpr int32 QuadsPerComp        = NumSubsections * SubsectionSizeQuads; // 126
    const     int32 RequiredSize        = Grid.Width; // accept any valid NxN

    if (Grid.Width != Grid.Height ||
        Grid.Width < QuadsPerComp + 1 ||
        ((Grid.Width - 1) % QuadsPerComp) != 0)
    {
        Result.Error = FString::Printf(
            TEXT("Grid %dx%d is not a valid landscape size — need NxN with (N-1) "
                 "a multiple of %d (e.g. 1009 or 2017)."),
            Grid.Width, Grid.Height, QuadsPerComp);
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

    UE_LOG(LogTemp, Display,
        TEXT("[GeoTerrain] LandscapeBuilder: grid %dx%d  elev %.1f..%.1f m  XYScale=%.2f cm/u  ZScale=%.2f cm/u"),
        Grid.Width, Grid.Height, Grid.ElevMin, Grid.ElevMax, XYScale, ZScale);

    // --- Centrar el PUNTO CENTRAL (lat/lon dado) en el (0,0,0) de Unreal -----
    // El origen del actor Landscape es la ESQUINA (vértice 0,0) y el terreno crece
    // hacia +X/+Y. Para que el centro geográfico caiga en el origen del mundo,
    // desplazamos el actor media anchura en X e Y. En Z lo bajamos para que la
    // elevación del punto central quede exactamente a Z=0 (el terreno es 1:1 real).
    const int32 N      = Grid.Width;                    // rejilla NxN
    const float HalfXY = (N - 1) * 0.5f * XYScale;      // media anchura en cm de mundo

    float CenterZOffset = 0.f;
    {
        const int32 CenterIdx = (Grid.Height / 2) * N + (N / 2);
        if (Grid.Data.IsValidIndex(CenterIdx))
        {
            const float Em = (Grid.ElevMin + Grid.ElevMax) * 0.5f; // elevación que cae en Z=0
            const float Ec = Grid.Data[CenterIdx];                 // elevación del centro
            CenterZOffset  = -(Ec - Em) * 100.f;                   // cm (1 m real = 100 cm)
        }
    }

    // NOTE: no fijar Params.Name — re-ejecutar chocaría con el actor previo. Usar el label.
    ALandscape* Landscape = World->SpawnActor<ALandscape>(
        FVector(-HalfXY, -HalfXY, CenterZOffset),
        FRotator::ZeroRotator,
        FActorSpawnParameters());

    if (!Landscape)
    {
        Result.Error = TEXT("Failed to spawn ALandscape actor.");
        return Result;
    }

    Landscape->SetActorScale3D(FVector(XYScale, XYScale, ZScale));
    Landscape->SetActorLabel(TEXT("GeoTerrain_Landscape"));

    // Assign a landscape material (e.g. an Auto-Landscape material) if provided.
    if (!MaterialPath.IsEmpty())
    {
        UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
        if (Mat)
        {
            Landscape->LandscapeMaterial = Mat;
            UE_LOG(LogTemp, Display, TEXT("[GeoTerrain] Landscape material assigned: %s"), *MaterialPath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[GeoTerrain] Could not load material: %s"), *MaterialPath);
        }
    }

    // UE5.8: bCanHaveLayersContent está deprecado. Un import sin edit layers se
    // consigue pasando un InImportLayers vacío a Import() (12º argumento, abajo).

    // --- Import heightmap ----------------------------------------------------
    // UE5.6 Import() takes per-layer TMaps. The base (non-edit) layer is keyed
    // by the DEFAULT FGuid() — using a fresh guid makes Import's internal
    // FindChecked() fail the "Pair != nullptr" assert (Map.h:716) and crash.
    const FGuid LandscapeGuid = FGuid::NewGuid();

    TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
    HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightData));

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
    MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

    Landscape->Import(
        LandscapeGuid,
        0, 0,
        RequiredSize - 1, RequiredSize - 1,
        NumSubsections,
        SubsectionSizeQuads,
        HeightDataPerLayer,
        /*HeightmapFileName=*/nullptr,
        MaterialLayerDataPerLayer,
        ELandscapeImportAlphamapType::Additive,
        TArrayView<const FLandscapeLayer>()   // UE5.8: sin edit layers
    );

    // Import() already sets up LandscapeInfo; fetch it (do not re-create).
    if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
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
