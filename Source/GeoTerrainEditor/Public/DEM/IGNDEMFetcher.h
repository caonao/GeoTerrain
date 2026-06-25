#pragma once

#include "CoreMinimal.h"
#include "DEM/CopernicusDEMFetcher.h"   // FElevationGrid, FOnElevationReady
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// ---------------------------------------------------------------------------
// Fetches the Spanish IGN MDT05 (5 m) elevation model via the INSPIRE WCS.
// One GetCoverage request returns a single int16 GeoTIFF (metres), which we
// decode and resample to the target landscape resolution.
//
// Coverage: Elevacion4258_5  (EPSG:4258 ≈ WGS84, 5 m). Spain only.
// Endpoint: https://servicios.idee.es/wcs-inspire/mdt
// ---------------------------------------------------------------------------
class FIGNDEMFetcher : public TSharedFromThis<FIGNDEMFetcher>
{
public:
    void FetchBBox(float InLatMin, float InLatMax, float InLonMin, float InLonMax,
                   FOnElevationReady InCallback, int32 TargetResolution = 1009);

    float GetProgress() const { return Progress; }

private:
    FOnElevationReady Callback;
    int32 TargetRes = 1009;
    float BBoxLatMin = 0.f, BBoxLatMax = 0.f, BBoxLonMin = 0.f, BBoxLonMax = 0.f;
    float Progress = 0.f;

    void OnResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected);

    // Minimal TIFF reader: little/big endian, uncompressed, 1 sample, 16-bit.
    static bool DecodeInt16TIFF(const TArray<uint8>& Data,
                                TArray<int16>& OutPixels,
                                int32& OutW, int32& OutH, FString& OutError);

    // Bilinear resample native grid → TargetRes×TargetRes FElevationGrid.
    FElevationGrid Resample(const TArray<int16>& Src, int32 W, int32 H) const;
};
