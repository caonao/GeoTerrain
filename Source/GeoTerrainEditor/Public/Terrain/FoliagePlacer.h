#pragma once

#include "CoreMinimal.h"
#include "DEM/CopernicusDEMFetcher.h"
#include "OSM/OSMDataFetcher.h"

class UStaticMesh;
class UWorld;

struct FFoliageRules
{
    float TreeLineAltitudeM =  2500.f; // above this → no trees
    float MinAltitudeM      =     0.f; // below this → no trees (sea/ocean)
    float MaxSlopeDeg       =    35.f; // slope above this → no trees
    float RoadExclusionM    =    50.f; // radius around roads free of trees (metres)
    float DensityPct        =    20.f; // 0–100: percentage of eligible pixels to populate
    int32 MaxInstances      = 20000;   // hard cap on tree count
    float ScaleMin          =   0.8f;
    float ScaleMax          =   1.4f;
};

// ---------------------------------------------------------------------------
// Places foliage instances on the landscape using AInstancedFoliageActor.
// If InMesh is nullptr, loads /Engine/BasicShapes/Cone as a placeholder.
// Trees are excluded near OSM roads and from steep/high-altitude areas.
// ---------------------------------------------------------------------------
class FFoliagePlacer
{
public:
    struct FPlaceResult
    {
        int32   InstancesPlaced = 0;
        FString Error;
        bool    bSuccess = false;
    };

    static FPlaceResult Place(const FElevationGrid& Grid,
                               float XYScaleCM,
                               const FFoliageRules& Rules,
                               const FOSMData* OSMData,   // may be nullptr
                               UStaticMesh* InMesh,
                               UWorld* World);

private:
    // Returns true if the world XY position is too close to any road node
    static bool IsNearRoad(const FVector2D& WorldXY, const FOSMData& OSMData,
                            const FElevationGrid& Grid, float XYScaleCM,
                            float ExclusionDistCM);

    static float ComputeSlopeDeg(const FElevationGrid& Grid, int32 Row, int32 Col, float CellSizeM);
};
