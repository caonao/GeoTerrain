#include "DEM/IGNDEMFetcher.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Math/UnrealMathUtility.h"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FIGNDEMFetcher::FetchBBox(float InLatMin, float InLatMax,
                                float InLonMin, float InLonMax,
                                FOnElevationReady InCallback, int32 TargetResolution)
{
    BBoxLatMin = InLatMin;
    BBoxLatMax = InLatMax;
    BBoxLonMin = InLonMin;
    BBoxLonMax = InLonMax;
    TargetRes  = TargetResolution;
    Callback   = InCallback;
    Progress   = 0.1f;

    // WCS 2.0.1 GetCoverage — coverage axes are "lat" then "long".
    const FString URL = FString::Printf(
        TEXT("https://servicios.idee.es/wcs-inspire/mdt")
        TEXT("?service=WCS&version=2.0.1&request=GetCoverage")
        TEXT("&coverageId=Elevacion4258_5")
        TEXT("&subset=lat(%.6f,%.6f)&subset=long(%.6f,%.6f)")
        TEXT("&format=image/tiff"),
        InLatMin, InLatMax, InLonMin, InLonMax);

    UE_LOG(LogTemp, Display, TEXT("[GeoTerrain] IGN WCS request: %s"), *URL);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(URL);
    Req->SetVerb(TEXT("GET"));
    Req->OnProcessRequestComplete().BindSP(this, &FIGNDEMFetcher::OnResponse);
    Req->ProcessRequest();
}

// ---------------------------------------------------------------------------
// HTTP callback
// ---------------------------------------------------------------------------

void FIGNDEMFetcher::OnResponse(FHttpRequestPtr /*Req*/, FHttpResponsePtr Resp, bool bConnected)
{
    Progress = 0.5f;

    FElevationGrid Fail;

    if (!bConnected || !Resp.IsValid())
    {
        Fail.Error = TEXT("IGN WCS request failed (no connection).");
        Callback.ExecuteIfBound(Fail);
        return;
    }
    if (Resp->GetResponseCode() != 200)
    {
        Fail.Error = FString::Printf(TEXT("IGN WCS returned HTTP %d."), Resp->GetResponseCode());
        Callback.ExecuteIfBound(Fail);
        return;
    }

    const TArray<uint8>& Body = Resp->GetContent();
    if (Body.Num() < 8 || !((Body[0] == 'I' && Body[1] == 'I') || (Body[0] == 'M' && Body[1] == 'M')))
    {
        // Likely an XML ServiceExceptionReport (area outside Spain or too large).
        Fail.Error = TEXT("IGN WCS did not return a TIFF — area may be outside Spain "
                          "or too large. Use a smaller bbox or switch to the global source.");
        Callback.ExecuteIfBound(Fail);
        return;
    }

    TArray<int16> Pixels; int32 W = 0, H = 0; FString Err;
    if (!DecodeInt16TIFF(Body, Pixels, W, H, Err))
    {
        Fail.Error = FString::Printf(TEXT("IGN TIFF decode failed: %s"), *Err);
        Callback.ExecuteIfBound(Fail);
        return;
    }

    UE_LOG(LogTemp, Display, TEXT("[GeoTerrain] IGN TIFF decoded: %dx%d px (native ~5 m)"), W, H);

    Progress = 0.9f;
    FElevationGrid Grid = Resample(Pixels, W, H);
    Callback.ExecuteIfBound(Grid);
}

// ---------------------------------------------------------------------------
// Minimal TIFF reader (uncompressed, 1 sample/pixel, 16-bit)
// ---------------------------------------------------------------------------

bool FIGNDEMFetcher::DecodeInt16TIFF(const TArray<uint8>& D,
                                      TArray<int16>& Out, int32& OutW, int32& OutH,
                                      FString& Err)
{
    const int32 N = D.Num();
    if (N < 8) { Err = TEXT("too small"); return false; }
    const uint8* P = D.GetData();
    const bool bLE = (P[0] == 'I' && P[1] == 'I');

    auto R16 = [&](int64 o) -> uint32
    {
        if (o < 0 || o + 1 >= N) return 0;
        return bLE ? (P[o] | (P[o + 1] << 8)) : ((P[o] << 8) | P[o + 1]);
    };
    auto R32 = [&](int64 o) -> uint32
    {
        if (o < 0 || o + 3 >= N) return 0;
        return bLE ? (P[o] | (P[o + 1] << 8) | (P[o + 2] << 16) | ((uint32)P[o + 3] << 24))
                   : (((uint32)P[o] << 24) | (P[o + 1] << 16) | (P[o + 2] << 8) | P[o + 3]);
    };

    const uint32 IFD = R32(4);
    if (IFD + 2 > (uint32)N) { Err = TEXT("bad IFD offset"); return false; }
    const uint32 Count = R16(IFD);

    struct FEntry { uint32 Type = 0, Cnt = 0, ValOff = 0; };
    TMap<uint32, FEntry> Tags;
    for (uint32 i = 0; i < Count; ++i)
    {
        const int64 e = (int64)IFD + 2 + (int64)i * 12;
        if (e + 12 > N) break;
        FEntry En;
        En.Type   = R16(e + 2);
        En.Cnt    = R32(e + 4);
        En.ValOff = (uint32)(e + 8);
        Tags.Add(R16(e), En);
    }

    auto ElemSize = [](uint32 T) -> uint32 { return T == 3 ? 2 : T == 4 ? 4 : T == 1 ? 1 : 4; };
    auto GetN = [&](uint32 Tag, uint32 Idx) -> uint32
    {
        const FEntry* En = Tags.Find(Tag);
        if (!En) return 0;
        const uint32 ES = ElemSize(En->Type);
        const uint32 Base = (En->Cnt * ES <= 4) ? En->ValOff : R32(En->ValOff);
        const int64  O = (int64)Base + (int64)Idx * ES;
        return En->Type == 3 ? R16(O) : En->Type == 4 ? R32(O) : (O < N ? P[O] : 0);
    };

    const uint32 Width  = GetN(256, 0);
    const uint32 Height = GetN(257, 0);
    const uint32 BPS    = Tags.Contains(258) ? GetN(258, 0) : 16;
    const uint32 Comp   = Tags.Contains(259) ? GetN(259, 0) : 1;
    const uint32 SF     = Tags.Contains(339) ? GetN(339, 0) : 2; // 2 = signed int

    if (Width == 0 || Height == 0) { Err = TEXT("zero dimensions"); return false; }
    if (BPS != 16) { Err = FString::Printf(TEXT("unsupported BitsPerSample %u"), BPS); return false; }
    if (Comp != 1) { Err = FString::Printf(TEXT("unsupported compression %u"), Comp); return false; }
    if ((int64)Width * Height > 64 * 1024 * 1024) { Err = TEXT("image too large"); return false; }

    const FEntry* SoE = Tags.Find(273);
    if (!SoE) { Err = TEXT("no StripOffsets"); return false; }
    const uint32 NStrips = SoE->Cnt;

    Out.SetNumZeroed((int32)(Width * Height));
    int32 Px = 0;
    const int32 Total = (int32)(Width * Height);

    for (uint32 s = 0; s < NStrips && Px < Total; ++s)
    {
        const uint32 Off = GetN(273, s);
        const uint32 Bytes = Tags.Contains(279) ? GetN(279, s) : (Width * Height * 2);
        const uint32 NVals = Bytes / 2;
        for (uint32 k = 0; k < NVals && Px < Total; ++k)
        {
            const int64 o = (int64)Off + (int64)k * 2;
            if (o + 1 >= N) break;
            const uint16 Raw = (uint16)R16(o);
            Out[Px++] = (SF == 2) ? (int16)Raw : (int16)(Raw & 0x7FFF);
        }
    }

    if (Px < Total) { Err = TEXT("truncated pixel data"); return false; }

    OutW = (int32)Width;
    OutH = (int32)Height;
    return true;
}

// ---------------------------------------------------------------------------
// Resample native grid → target FElevationGrid (row 0 = north)
// ---------------------------------------------------------------------------

FElevationGrid FIGNDEMFetcher::Resample(const TArray<int16>& Src, int32 W, int32 H) const
{
    FElevationGrid Out;
    if (W <= 1 || H <= 1 || Src.Num() < W * H)
    {
        Out.Error = TEXT("invalid native grid for resample");
        return Out;
    }

    Out.Width  = TargetRes;
    Out.Height = TargetRes;
    Out.LatMin = BBoxLatMin;
    Out.LatMax = BBoxLatMax;
    Out.LonMin = BBoxLonMin;
    Out.LonMax = BBoxLonMax;
    Out.Data.SetNumUninitialized(TargetRes * TargetRes);

    // GeoTIFF is north-up: native row 0 = north (LatMax), col 0 = west (LonMin).
    // Output row 0 = north too, so the row mapping is direct.
    float EMin = 1e9f, EMax = -1e9f;

    for (int32 r = 0; r < TargetRes; ++r)
    {
        const float SrcY = (r / (float)(TargetRes - 1)) * (H - 1);
        const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SrcY), 0, H - 1);
        const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
        const float FY = SrcY - Y0;

        for (int32 c = 0; c < TargetRes; ++c)
        {
            const float SrcX = (c / (float)(TargetRes - 1)) * (W - 1);
            const int32 X0 = FMath::Clamp(FMath::FloorToInt(SrcX), 0, W - 1);
            const int32 X1 = FMath::Min(X0 + 1, W - 1);
            const float FX = SrcX - X0;

            const float E00 = (float)Src[Y0 * W + X0];
            const float E10 = (float)Src[Y0 * W + X1];
            const float E01 = (float)Src[Y1 * W + X0];
            const float E11 = (float)Src[Y1 * W + X1];
            const float E   = FMath::BiLerp(E00, E10, E01, E11, FX, FY);

            Out.Data[r * TargetRes + c] = E;
            EMin = FMath::Min(EMin, E);
            EMax = FMath::Max(EMax, E);
        }
    }

    Out.ElevMin = EMin;
    Out.ElevMax = EMax;
    Out.bValid  = true;

    UE_LOG(LogTemp, Display,
        TEXT("[GeoTerrain] IGN final grid %dx%d  elevation %.1f..%.1f m (span %.1f m)"),
        Out.Width, Out.Height, Out.ElevMin, Out.ElevMax, Out.ElevMax - Out.ElevMin);

    return Out;
}
