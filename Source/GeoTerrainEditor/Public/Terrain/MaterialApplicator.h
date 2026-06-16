#pragma once

#include "CoreMinimal.h"
#include "DEM/CopernicusDEMFetcher.h"

class ALandscape;
class ULandscapeInfo;
class ULandscapeLayerInfoObject;

// Altitude/slope thresholds that drive which material layer is dominant at each vertex.
struct FMaterialRules
{
    float SnowAltitudeM  = 2000.f; // above this → Snow layer
    float RockAltitudeM  = 1200.f; // above this (or steep slope) → Rock layer
    float RockSlopeDeg   =   40.f; // slope in degrees above which → Rock
    float DirtSlopeDeg   =   60.f; // slope in degrees above which → bare Dirt/cliff
};

// ---------------------------------------------------------------------------
// Paints landscape weight maps (alpha layers) based on altitude and slope.
//
// Creates four ULandscapeLayerInfoObject assets named: Snow, Rock, Grass, Dirt
// stored inside the landscape package.  Assign a landscape material that uses
// these layer names to see the results.
// ---------------------------------------------------------------------------
class FMaterialApplicator
{
public:
    struct FApplyResult
    {
        bool    bSuccess    = false;
        FString Error;
        int32   SnowPixels  = 0;
        int32   RockPixels  = 0;
        int32   GrassPixels = 0;
        int32   DirtPixels  = 0;
    };

    static FApplyResult Apply(ALandscape* Landscape,
                               const FElevationGrid& Grid,
                               const FMaterialRules& Rules,
                               float XYScaleCM);

private:
    static float ComputeSlopeDeg(const FElevationGrid& Grid, int32 Row, int32 Col, float CellSizeM);
    static ULandscapeLayerInfoObject* GetOrCreateLayerInfo(ULandscapeInfo* Info, FName LayerName,
                                                            ALandscape* Landscape);
};
