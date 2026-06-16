#include "OSM/OSMDataFetcher.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Json.h"

// ---------------------------------------------------------------------------
// Query builder
// ---------------------------------------------------------------------------

FString FOSMDataFetcher::BuildOverpassQuery(float LatMin, float LatMax,
                                             float LonMin, float LonMax)
{
    // Overpass bbox order: south, west, north, east
    const FString BBox = FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f"),
                                         LatMin, LonMin, LatMax, LonMax);

    return FString::Printf(
        TEXT("[out:json][timeout:60];\n"
             "(\n"
             "  way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential)$\"](%s);\n"
             "  way[\"landuse\"~\"^(forest|wood)$\"](%s);\n"
             "  way[\"natural\"~\"^(wood|water)$\"](%s);\n"
             "  >;\n"
             ");\n"
             "out body;"),
        *BBox, *BBox, *BBox);
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

void FOSMDataFetcher::FetchBBox(float LatMin, float LatMax,
                                 float LonMin, float LonMax,
                                 FOnOSMReady InCallback)
{
    const FString Query = BuildOverpassQuery(LatMin, LatMax, LonMin, LonMax);
    const FString Body  = TEXT("data=") + FGenericPlatformHttp::UrlEncode(Query);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(TEXT("https://overpass-api.de/api/interpreter"));
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
    Req->SetContentAsString(Body);
    Req->OnProcessRequestComplete().BindRaw(
        this, &FOSMDataFetcher::OnResponse, InCallback);
    Req->ProcessRequest();
}

void FOSMDataFetcher::OnResponse(FHttpRequestPtr /*Req*/, FHttpResponsePtr Resp,
                                  bool bConnected, FOnOSMReady InCallback)
{
    if (!bConnected || !Resp.IsValid())
    {
        FOSMData Err;
        Err.Error = TEXT("Overpass API request failed (no connection).");
        InCallback.ExecuteIfBound(Err);
        return;
    }

    if (Resp->GetResponseCode() != 200)
    {
        FOSMData Err;
        Err.Error = FString::Printf(TEXT("Overpass API returned HTTP %d."),
                                    Resp->GetResponseCode());
        InCallback.ExecuteIfBound(Err);
        return;
    }

    FOSMData Data = ParseJSON(Resp->GetContentAsString());
    InCallback.ExecuteIfBound(Data);
}

// ---------------------------------------------------------------------------
// JSON parser
// ---------------------------------------------------------------------------

FOSMData FOSMDataFetcher::ParseJSON(const FString& JSON)
{
    FOSMData Out;

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSON);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        Out.Error = TEXT("Failed to parse Overpass JSON response.");
        return Out;
    }

    const TArray<TSharedPtr<FJsonValue>>* Elements;
    if (!Root->TryGetArrayField(TEXT("elements"), Elements))
    {
        Out.Error = TEXT("No 'elements' array in Overpass response.");
        return Out;
    }

    for (const TSharedPtr<FJsonValue>& ElemVal : *Elements)
    {
        const TSharedPtr<FJsonObject>* ElemObj;
        if (!ElemVal->TryGetObject(ElemObj)) continue;

        FString Type;
        if (!(*ElemObj)->TryGetStringField(TEXT("type"), Type)) continue;

        // --- node ---
        if (Type == TEXT("node"))
        {
            FOSMNode Node;
            double IdDouble = 0.0;
            (*ElemObj)->TryGetNumberField(TEXT("id"),  IdDouble);
            (*ElemObj)->TryGetNumberField(TEXT("lat"), Node.Lat);
            (*ElemObj)->TryGetNumberField(TEXT("lon"), Node.Lon);
            Node.Id = (int64)IdDouble;
            if (Node.Id != 0) Out.Nodes.Add(Node.Id, Node);
        }
        // --- way ---
        else if (Type == TEXT("way"))
        {
            FOSMWay Way;
            double IdDouble = 0.0;
            (*ElemObj)->TryGetNumberField(TEXT("id"), IdDouble);
            Way.Id = (int64)IdDouble;

            // Node list
            const TArray<TSharedPtr<FJsonValue>>* NodeIds;
            if ((*ElemObj)->TryGetArrayField(TEXT("nodes"), NodeIds))
            {
                for (const auto& NV : *NodeIds)
                {
                    double NodeIdD = 0.0;
                    NV->TryGetNumber(NodeIdD);
                    Way.NodeIds.Add((int64)NodeIdD);
                }
            }

            // Tags
            const TSharedPtr<FJsonObject>* Tags;
            if ((*ElemObj)->TryGetObjectField(TEXT("tags"), Tags))
            {
                FString HW, LU, Nat;
                if ((*Tags)->TryGetStringField(TEXT("highway"), HW))
                {
                    Way.Type         = EOSMWayType::Road;
                    Way.HighwayClass = HW;
                    ++Out.RoadCount;
                }
                else if ((*Tags)->TryGetStringField(TEXT("landuse"), LU) &&
                         (LU == TEXT("forest") || LU == TEXT("wood")))
                {
                    Way.Type = EOSMWayType::Forest;
                    ++Out.ForestCount;
                }
                else if ((*Tags)->TryGetStringField(TEXT("natural"), Nat) &&
                         (Nat == TEXT("wood") || Nat == TEXT("water")))
                {
                    Way.Type = (Nat == TEXT("water")) ? EOSMWayType::Water : EOSMWayType::Forest;
                    if (Way.Type == EOSMWayType::Water)  ++Out.WaterCount;
                    else                                  ++Out.ForestCount;
                }
                else
                {
                    Way.Type = EOSMWayType::Unknown;
                }
            }

            if (Way.NodeIds.Num() >= 2) Out.Ways.Add(Way);
        }
    }

    Out.bValid = true;
    return Out;
}
