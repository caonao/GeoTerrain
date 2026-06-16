#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// ---------------------------------------------------------------------------
// Output structure — elevation grid in geographic order (north → south rows)
// ---------------------------------------------------------------------------
struct FElevationGrid
{
    TArray<float> Data;      // row-major: row 0 = north edge, col 0 = west edge
    int32         Width  = 0;
    int32         Height = 0;
    float         LatMin = 0.f;
    float         LatMax = 0.f;
    float         LonMin = 0.f;
    float         LonMax = 0.f;
    float         ElevMin =  9999.f;
    float         ElevMax = -9999.f;
    bool          bValid  = false;
    FString       Error;
};

DECLARE_DELEGATE_OneParam(FOnElevationReady, const FElevationGrid&);

// ---------------------------------------------------------------------------
// Downloads AWS Elevation Tiles (Terrarium RGB-PNG, no API key required).
// Source: https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{y}/{x}.png
// Elevation = (R * 256 + G + B / 256) - 32768  (metres)
// ---------------------------------------------------------------------------
class FCopernicusDEMFetcher : public TSharedFromThis<FCopernicusDEMFetcher>
{
public:
    // TargetResolution must be a valid UE5 landscape size: 127, 253, 505, 1009, 2017
    void FetchBBox(float InLatMin, float InLatMax, float InLonMin, float InLonMax,
                   FOnElevationReady InCallback, int32 TargetResolution = 1009);

    // Progress 0..1, polled from UI
    float GetProgress() const;

private:
    struct FTileState
    {
        int32         TileX = 0;
        int32         TileY = 0;
        int32         Zoom  = 0;
        TArray<uint8> PNGData;
        bool          bDone = false;
        bool          bOK   = false;
    };

    TArray<TSharedPtr<FTileState>> Tiles;
    int32                          TotalTiles   = 0;
    TAtomic<int32>                 DoneTiles    { 0 };
    int32                          TargetRes    = 1009;
    float                          BBoxLatMin   = 0.f;
    float                          BBoxLatMax   = 0.f;
    float                          BBoxLonMin   = 0.f;
    float                          BBoxLonMax   = 0.f;
    FOnElevationReady              Callback;

    void OnTileResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected,
                        TSharedPtr<FTileState> State);
    void TryFinalize();

    // ----- static helpers -----
    static FIntPoint  LatLonToTileXY(float Lat, float Lon, int32 Zoom);
    static void       TileXYToNWCorner(int32 TileX, int32 TileY, int32 Zoom,
                                       float& OutLat, float& OutLon);
    static FORCEINLINE float DecodeTerrarium(uint8 R, uint8 G, uint8 B)
    {
        return R * 256.f + G + B / 256.f - 32768.f;
    }

    FElevationGrid StitchCropResample() const;
};
