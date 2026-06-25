#pragma once

#include "CoreMinimal.h"
#include "DEM/CopernicusDEMFetcher.h"

class ALandscape;

// ---------------------------------------------------------------------------
// Converts an FElevationGrid into a UE5 ALandscape actor in the active level.
// Grid must be exactly 1009x1009 (or another valid landscape size passed in).
// ---------------------------------------------------------------------------
class FLandscapeBuilder
{
public:
    struct FBuildResult
    {
        ALandscape* Landscape = nullptr;
        FString     Error;
        bool        bSuccess  = false;

        // Actual world scale applied (cm per vertex XY, cm per unit Z)
        float XYScaleCM = 100.f;
        float ZScaleCM  = 100.f;
    };

    // MaterialPath: optional object path to a landscape material/instance to
    // assign (e.g. an Auto-Landscape material). Empty = leave default (grey).
    static FBuildResult Build(const FElevationGrid& Grid,
                              const FString& MaterialPath = FString());

private:
    // Convert float elevations to UE5 uint16 heightmap (0=min, 65535=max)
    static TArray<uint16> ToHeightmap(const TArray<float>& Elevations,
                                      float ElevMin, float ElevMax);

    // XY scale: cm per landscape unit so the mesh matches real-world size
    static float ComputeXYScale(const FElevationGrid& Grid);

    // Z scale: maps 0..65535 heightmap range to the real elevation span in cm
    static float ComputeZScale(float ElevMin, float ElevMax);
};
