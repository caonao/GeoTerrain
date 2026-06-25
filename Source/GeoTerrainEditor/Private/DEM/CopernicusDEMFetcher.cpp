#include "DEM/CopernicusDEMFetcher.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Math/UnrealMathUtility.h"

// ---------------------------------------------------------------------------
// Tile math (Web Mercator / Slippy map convention)
// ---------------------------------------------------------------------------

FIntPoint FCopernicusDEMFetcher::LatLonToTileXY(float Lat, float Lon, int32 Zoom)
{
    const int32  N   = 1 << Zoom;
    const double X   = (Lon + 180.0) / 360.0 * N;
    const double LatR = Lat * (PI / 180.0);
    const double Y   = (1.0 - FMath::LogX(exp(1.0), FMath::Tan(LatR) + 1.0 / FMath::Cos(LatR)) / PI) / 2.0 * N;
    return FIntPoint(FMath::FloorToInt(X), FMath::FloorToInt(Y));
}

void FCopernicusDEMFetcher::TileXYToNWCorner(int32 TileX, int32 TileY, int32 Zoom,
                                              float& OutLat, float& OutLon)
{
    const int32  N    = 1 << Zoom;
    OutLon = TileX / (float)N * 360.f - 180.f;
    const double NRad = FMath::Atan(FMath::Sinh(PI * (1.f - 2.f * TileY / (float)N)));
    OutLat = (float)(NRad * (180.0 / PI));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FCopernicusDEMFetcher::FetchBBox(float InLatMin, float InLatMax,
                                       float InLonMin, float InLonMax,
                                       FOnElevationReady InCallback,
                                       int32 TargetResolution)
{
    BBoxLatMin = InLatMin;
    BBoxLatMax = InLatMax;
    BBoxLonMin = InLonMin;
    BBoxLonMax = InLonMax;
    TargetRes  = TargetResolution;
    Callback   = InCallback;
    Tiles.Empty();
    DoneTiles  = 0;

    // Adaptive zoom: pick a zoom whose source pixels roughly match the output
    // resolution across the bbox, so small areas get real detail instead of a
    // few zoom-10 samples upsampled to 1009 (which looks low-res / stepped).
    const double SpanDeg = FMath::Max((double)(InLonMax - InLonMin),
                                      (double)(InLatMax - InLatMin));
    int32 Zoom;
    {
        const double TwoPowZ = (double)TargetResolution * 360.0
                             / (FMath::Max(SpanDeg, 1e-4) * 256.0);
        Zoom = FMath::Clamp((int32)FMath::RoundToInt(FMath::Log2(TwoPowZ)), 8, 14);
    }

    // NW tile = (LatMax, LonMin), SE tile = (LatMin, LonMax). In Mercator,
    // higher lat → lower tile Y. Drop a zoom level if the tile count is huge.
    FIntPoint TileNW = LatLonToTileXY(InLatMax, InLonMin, Zoom);
    FIntPoint TileSE = LatLonToTileXY(InLatMin, InLonMax, Zoom);
    while (Zoom > 8 &&
           (TileSE.X - TileNW.X + 1) * (TileSE.Y - TileNW.Y + 1) > 144)
    {
        --Zoom;
        TileNW = LatLonToTileXY(InLatMax, InLonMin, Zoom);
        TileSE = LatLonToTileXY(InLatMin, InLonMax, Zoom);
    }

    for (int32 TY = TileNW.Y; TY <= TileSE.Y; ++TY)
    {
        for (int32 TX = TileNW.X; TX <= TileSE.X; ++TX)
        {
            TSharedPtr<FTileState> State = MakeShared<FTileState>();
            State->TileX = TX;
            State->TileY = TY;
            State->Zoom  = Zoom;
            Tiles.Add(State);

            // AWS Terrarium tile URL order is {z}/{x}/{y}.png — NOT z/y/x.
            // Swapping x/y fetches the mirror tile (e.g. mid-Atlantic instead
            // of the requested land), producing bogus ocean-floor elevations.
            const FString URL = FString::Printf(
                TEXT("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png"),
                Zoom, TX, TY);

            TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
            Req->SetURL(URL);
            Req->SetVerb(TEXT("GET"));
            Req->OnProcessRequestComplete().BindSP(
                this, &FCopernicusDEMFetcher::OnTileResponse, State);
            Req->ProcessRequest();
        }
    }

    TotalTiles = Tiles.Num();

    UE_LOG(LogTemp, Display,
        TEXT("[GeoTerrain] FetchBBox lat[%.4f..%.4f] lon[%.4f..%.4f] z%d  tilesX[%d..%d] tilesY[%d..%d] = %d tiles"),
        InLatMin, InLatMax, InLonMin, InLonMax, Zoom,
        TileNW.X, TileSE.X, TileNW.Y, TileSE.Y, TotalTiles);

    if (TotalTiles == 0)
    {
        FElevationGrid Empty;
        Empty.Error = TEXT("No tiles found for the given bounding box.");
        Callback.ExecuteIfBound(Empty);
    }
}

float FCopernicusDEMFetcher::GetProgress() const
{
    return (TotalTiles > 0) ? (float)DoneTiles.Load() / TotalTiles : 0.f;
}

// ---------------------------------------------------------------------------
// HTTP callback
// ---------------------------------------------------------------------------

void FCopernicusDEMFetcher::OnTileResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp,
                                            bool bConnected, TSharedPtr<FTileState> State)
{
    if (bConnected && Resp.IsValid() && Resp->GetResponseCode() == 200)
    {
        State->PNGData = Resp->GetContent();
        State->bOK     = true;
    }
    State->bDone = true;
    ++DoneTiles;
    TryFinalize();
}

void FCopernicusDEMFetcher::TryFinalize()
{
    if (DoneTiles.Load() < TotalTiles) return;

    FElevationGrid Result = StitchCropResample();
    Callback.ExecuteIfBound(Result);
}

// ---------------------------------------------------------------------------
// Stitch + crop + bilinear resample
// ---------------------------------------------------------------------------

FElevationGrid FCopernicusDEMFetcher::StitchCropResample() const
{
    FElevationGrid Out;

    if (Tiles.Num() == 0)
    {
        Out.Error = TEXT("No tiles to stitch.");
        return Out;
    }

    // Tile grid extents
    int32 MinTX = INT32_MAX, MaxTX = INT32_MIN;
    int32 MinTY = INT32_MAX, MaxTY = INT32_MIN;
    for (const auto& T : Tiles)
    {
        MinTX = FMath::Min(MinTX, T->TileX);
        MaxTX = FMath::Max(MaxTX, T->TileX);
        MinTY = FMath::Min(MinTY, T->TileY);
        MaxTY = FMath::Max(MaxTY, T->TileY);
    }

    constexpr int32 TilePx  = 256;
    const     int32 GridW   = (MaxTX - MinTX + 1) * TilePx;
    const     int32 GridH   = (MaxTY - MinTY + 1) * TilePx;

    TArray<float> Stitched;
    Stitched.SetNumZeroed(GridW * GridH);

    IImageWrapperModule& IWM =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

    int32 DecodedTiles = 0;
    for (const auto& State : Tiles)
    {
        if (!State->bOK || State->PNGData.Num() == 0) continue;

        TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper.IsValid()) continue;
        if (!Wrapper->SetCompressed(State->PNGData.GetData(), State->PNGData.Num())) continue;

        // ERGBFormat::RGB was removed in UE5.6 — request RGBA (stride 4) and use R,G,B.
        TArray<uint8> Raw;
        if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, Raw)) continue;
        if (Raw.Num() < TilePx * TilePx * 4) continue;

        const int32 OffX = (State->TileX - MinTX) * TilePx;
        const int32 OffY = (State->TileY - MinTY) * TilePx;

        for (int32 PY = 0; PY < TilePx; ++PY)
        {
            for (int32 PX = 0; PX < TilePx; ++PX)
            {
                const int32 Src = (PY * TilePx + PX) * 4; // RGBA
                Stitched[(OffY + PY) * GridW + (OffX + PX)] =
                    DecodeTerrarium(Raw[Src], Raw[Src + 1], Raw[Src + 2]);
            }
        }
        ++DecodedTiles;
    }

    int32 OkTiles = 0;
    for (const auto& T : Tiles) { if (T->bOK) ++OkTiles; }
    UE_LOG(LogTemp, Display,
        TEXT("[GeoTerrain] Tiles requested=%d ok=%d decoded=%d"),
        Tiles.Num(), OkTiles, DecodedTiles);

    if (DecodedTiles == 0)
    {
        Out.Error = TEXT("All tile downloads failed. Check your internet connection.");
        return Out;
    }

    // Neutralize Terrarium "nodata" pixels (decoded near -32768 m). Left untouched
    // they explode the elevation range, so after 0..65535 normalization the real
    // relief collapses into a near-flat band. Replace them with the valid minimum.
    {
        float ValidMin = 1e9f, ValidMax = -1e9f;
        for (float V : Stitched)
        {
            if (V > -12000.f) { ValidMin = FMath::Min(ValidMin, V); ValidMax = FMath::Max(ValidMax, V); }
        }
        if (ValidMin > 1e8f) { ValidMin = 0.f; ValidMax = 0.f; }

        int32 NodataCount = 0;
        for (float& V : Stitched)
        {
            if (V <= -12000.f) { V = ValidMin; ++NodataCount; }
        }
        UE_LOG(LogTemp, Display,
            TEXT("[GeoTerrain] Stitched valid range %.1f..%.1f m, nodata pixels fixed=%d"),
            ValidMin, ValidMax, NodataCount);
    }

    // Geographic extents of the full stitched grid (use the actual zoom used).
    const int32 Z = (Tiles.Num() > 0) ? Tiles[0]->Zoom : 10;
    float GridLatNW, GridLonNW, GridLatSE, GridLonSE;
    TileXYToNWCorner(MinTX,     MinTY,     Z, GridLatNW, GridLonNW);
    TileXYToNWCorner(MaxTX + 1, MaxTY + 1, Z, GridLatSE, GridLonSE);
    // GridLatNW > GridLatSE (north > south), GridLonNW < GridLonSE (west < east)

    // Map world coords to pixel coords within the stitched image
    // Row 0 = north edge (GridLatNW), Row GridH-1 = south edge (GridLatSE)
    auto LatToPixelF = [&](float Lat) -> float {
        return (GridLatNW - Lat) / (GridLatNW - GridLatSE) * (GridH - 1);
    };
    auto LonToPixelF = [&](float Lon) -> float {
        return (Lon - GridLonNW) / (GridLonSE - GridLonNW) * (GridW - 1);
    };

    const float SrcX0 = LonToPixelF(BBoxLonMin);  // west edge in stitched pixels
    const float SrcX1 = LonToPixelF(BBoxLonMax);  // east
    const float SrcY0 = LatToPixelF(BBoxLatMax);  // north (small Y)
    const float SrcY1 = LatToPixelF(BBoxLatMin);  // south (large Y)

    // Bilinear resample crop → TargetRes x TargetRes
    Out.Width  = TargetRes;
    Out.Height = TargetRes;
    Out.LatMin = BBoxLatMin;
    Out.LatMax = BBoxLatMax;
    Out.LonMin = BBoxLonMin;
    Out.LonMax = BBoxLonMax;
    Out.Data.SetNumUninitialized(TargetRes * TargetRes);

    float EMin =  1e9f, EMax = -1e9f;

    for (int32 DstRow = 0; DstRow < TargetRes; ++DstRow)
    {
        const float SrcY = SrcY0 + (DstRow / (float)(TargetRes - 1)) * (SrcY1 - SrcY0);
        const int32 Y0   = FMath::Clamp(FMath::FloorToInt(SrcY), 0, GridH - 1);
        const int32 Y1   = FMath::Clamp(Y0 + 1, 0, GridH - 1);
        const float FY   = SrcY - Y0;

        for (int32 DstCol = 0; DstCol < TargetRes; ++DstCol)
        {
            const float SrcX = SrcX0 + (DstCol / (float)(TargetRes - 1)) * (SrcX1 - SrcX0);
            const int32 X0   = FMath::Clamp(FMath::FloorToInt(SrcX), 0, GridW - 1);
            const int32 X1   = FMath::Clamp(X0 + 1, 0, GridW - 1);
            const float FX   = SrcX - X0;

            const float E00 = Stitched[Y0 * GridW + X0];
            const float E10 = Stitched[Y0 * GridW + X1];
            const float E01 = Stitched[Y1 * GridW + X0];
            const float E11 = Stitched[Y1 * GridW + X1];
            const float E   = FMath::BiLerp(E00, E10, E01, E11, FX, FY);

            Out.Data[DstRow * TargetRes + DstCol] = E;
            EMin = FMath::Min(EMin, E);
            EMax = FMath::Max(EMax, E);
        }
    }

    Out.ElevMin = EMin;
    Out.ElevMax = EMax;
    Out.bValid  = true;

    UE_LOG(LogTemp, Display,
        TEXT("[GeoTerrain] Final grid %dx%d  elevation %.1f..%.1f m (span %.1f m)"),
        Out.Width, Out.Height, Out.ElevMin, Out.ElevMax, Out.ElevMax - Out.ElevMin);

    return Out;
}
