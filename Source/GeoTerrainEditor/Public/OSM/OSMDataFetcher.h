#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct FOSMNode
{
    int64  Id  = 0;
    double Lat = 0.0;
    double Lon = 0.0;
};

enum class EOSMWayType : uint8
{
    Road,
    Forest,
    Water,
    Unknown
};

struct FOSMWay
{
    int64           Id           = 0;
    TArray<int64>   NodeIds;
    EOSMWayType     Type         = EOSMWayType::Unknown;
    FString         HighwayClass; // "motorway", "primary", "residential", etc.
};

struct FOSMData
{
    TMap<int64, FOSMNode> Nodes;
    TArray<FOSMWay>       Ways;

    int32   RoadCount   = 0;
    int32   ForestCount = 0;
    int32   WaterCount  = 0;

    bool    bValid = false;
    FString Error;
};

DECLARE_DELEGATE_OneParam(FOnOSMReady, const FOSMData&);

// ---------------------------------------------------------------------------
// Fetcher — uses the public Overpass API, no authentication required.
// Query: roads + forests + water bodies inside the bbox.
// ---------------------------------------------------------------------------
class FOSMDataFetcher
{
public:
    void FetchBBox(float LatMin, float LatMax, float LonMin, float LonMax,
                   FOnOSMReady InCallback);

private:
    void OnResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp,
                    bool bConnected, FOnOSMReady InCallback);

    static FString  BuildOverpassQuery(float LatMin, float LatMax,
                                       float LonMin, float LonMax);
    static FOSMData ParseJSON(const FString& JSON);
};
