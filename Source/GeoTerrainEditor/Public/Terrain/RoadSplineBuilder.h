#pragma once

#include "CoreMinimal.h"
#include "OSM/OSMDataFetcher.h"
#include "DEM/CopernicusDEMFetcher.h"

class ALandscape;

// ---------------------------------------------------------------------------
// Converts FOSMData ways into USplineComponent actors placed over the landscape.
//
// Coordinate mapping (matches LandscapeBuilder.cpp):
//   World X = (Lon - LonMin) / (LonMax - LonMin) * (Width-1) * XYScaleCM
//   World Y = (LatMax - Lat) / (LatMax - LatMin) * (Height-1) * XYScaleCM
//   World Z = elevation_metres * 100 + height_offset_cm
// ---------------------------------------------------------------------------
class FRoadSplineBuilder
{
public:
    struct FBuildParams
    {
        const FOSMData*      OSM         = nullptr;
        const FElevationGrid* Grid       = nullptr;
        float                XYScaleCM  = 100.f;
        float                RoadOffsetCM = 50.f;   // cm above terrain surface
        bool                 bBuildRoads  = true;
        bool                 bBuildForests = true;
        bool                 bBuildWater   = true;
        int32                MaxWaysPerType = 500;   // safety cap
    };

    struct FBuildResult
    {
        int32   RoadsBuilt   = 0;
        int32   ForestsBuilt = 0;
        int32   WaterBuilt   = 0;
        FString Error;
        bool    bSuccess = false;
    };

    static FBuildResult Build(const FBuildParams& Params, UWorld* World);

private:
    // World-space position for a lat/lon using the elevation grid
    static FVector LatLonToWorld(double Lat, double Lon,
                                  const FElevationGrid& Grid,
                                  float XYScaleCM, float ZOffsetCM);

    // Simplify a polyline by removing points closer than MinDistCM
    static TArray<FVector> SimplifyPath(const TArray<FVector>& Points, float MinDistCM = 200.f);

    // Create one spline actor in the world
    static void SpawnSplineActor(UWorld* World, const TArray<FVector>& Points,
                                  const FName& ActorLabel, const FLinearColor& SplineColor,
                                  AActor* ParentFolder);

    // Spawns a folder-like empty actor as parent
    static AActor* SpawnParentActor(UWorld* World, const FString& Label);
};
