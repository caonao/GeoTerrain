#include "Terrain/RoadSplineBuilder.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

// ---------------------------------------------------------------------------
// Coordinate math
// ---------------------------------------------------------------------------

FVector FRoadSplineBuilder::LatLonToWorld(double Lat, double Lon,
                                           const FElevationGrid& Grid,
                                           float XYScaleCM, float ZOffsetCM)
{
    // Normalised position in grid (0..1)
    const float NX = (float)((Lon - Grid.LonMin) / (Grid.LonMax - Grid.LonMin));
    const float NY = (float)((Grid.LatMax - Lat)  / (Grid.LatMax - Grid.LatMin));

    // World XY (cm)
    const float WX = NX * (Grid.Width  - 1) * XYScaleCM;
    const float WY = NY * (Grid.Height - 1) * XYScaleCM;

    // Elevation: sample bilinear from grid, convert m → cm
    const float ColF = NX * (Grid.Width  - 1);
    const float RowF = NY * (Grid.Height - 1);

    const int32 C0 = FMath::Clamp(FMath::FloorToInt(ColF), 0, Grid.Width  - 1);
    const int32 R0 = FMath::Clamp(FMath::FloorToInt(RowF), 0, Grid.Height - 1);
    const int32 C1 = FMath::Min(C0 + 1, Grid.Width  - 1);
    const int32 R1 = FMath::Min(R0 + 1, Grid.Height - 1);
    const float FC = ColF - C0;
    const float FR = RowF - R0;

    const float E00 = Grid.Data[R0 * Grid.Width + C0];
    const float E10 = Grid.Data[R0 * Grid.Width + C1];
    const float E01 = Grid.Data[R1 * Grid.Width + C0];
    const float E11 = Grid.Data[R1 * Grid.Width + C1];
    const float Elev = FMath::BiLerp(E00, E10, E01, E11, FC, FR);

    const float WZ = Elev * 100.f + ZOffsetCM;

    return FVector(WX, WY, WZ);
}

// ---------------------------------------------------------------------------
// Path simplification (removes redundant points closer than MinDistCM)
// ---------------------------------------------------------------------------

TArray<FVector> FRoadSplineBuilder::SimplifyPath(const TArray<FVector>& Points, float MinDistCM)
{
    if (Points.Num() < 2) return Points;

    TArray<FVector> Out;
    Out.Add(Points[0]);
    const float MinDistSq = MinDistCM * MinDistCM;

    for (int32 i = 1; i < Points.Num() - 1; ++i)
    {
        if (FVector::DistSquared(Out.Last(), Points[i]) >= MinDistSq)
            Out.Add(Points[i]);
    }
    Out.Add(Points.Last()); // always keep last point
    return Out;
}

// ---------------------------------------------------------------------------
// Spline actor creation
// ---------------------------------------------------------------------------

void FRoadSplineBuilder::SpawnSplineActor(UWorld* World, const TArray<FVector>& Points,
                                           const FName& ActorLabel,
                                           const FLinearColor& SplineColor,
                                           AActor* /*ParentFolder*/)
{
    FActorSpawnParameters Params;
    Params.Name = ActorLabel;

    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(),
                                              FTransform::Identity, Params);
    if (!Actor) return;

    Actor->SetActorLabel(ActorLabel.ToString());

    USplineComponent* Spline = NewObject<USplineComponent>(Actor, USplineComponent::StaticClass());
    Spline->RegisterComponent();
    Actor->SetRootComponent(Spline);

    // Set spline points
    Spline->ClearSplinePoints(false);
    for (int32 i = 0; i < Points.Num(); ++i)
    {
        Spline->AddSplinePoint(Points[i], ESplineCoordinateSpace::World, false);
    }
    // Straight tangents look better for roads than curved interpolation
    for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
    {
        Spline->SetSplinePointType(i, ESplinePointType::Linear, false);
    }
    Spline->UpdateSpline();

#if WITH_EDITOR
    Spline->EditorUnselectedSplineSegmentColor = SplineColor;
    Spline->EditorSelectedSplineSegmentColor   = FLinearColor::White;
#endif
}

AActor* FRoadSplineBuilder::SpawnParentActor(UWorld* World, const FString& Label)
{
    FActorSpawnParameters Params;
    Params.Name = FName(*Label);
    AActor* Parent = World->SpawnActor<AActor>(AActor::StaticClass(),
                                               FTransform::Identity, Params);
    if (Parent) Parent->SetActorLabel(Label);
    return Parent;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

FRoadSplineBuilder::FBuildResult FRoadSplineBuilder::Build(const FBuildParams& Params,
                                                            UWorld* World)
{
    FBuildResult Result;

    if (!Params.OSM || !Params.Grid)
    {
        Result.Error = TEXT("Invalid params: null OSM or Grid.");
        return Result;
    }
    if (!Params.OSM->bValid)
    {
        Result.Error = FString::Printf(TEXT("OSM data invalid: %s"), *Params.OSM->Error);
        return Result;
    }
    if (!Params.Grid->bValid)
    {
        Result.Error = TEXT("Elevation grid is not valid.");
        return Result;
    }
    if (!World)
    {
        Result.Error = TEXT("No world provided.");
        return Result;
    }

    // Colour coding by way type
    static const TMap<FString, FLinearColor> HighwayColors = {
        { TEXT("motorway"),      FLinearColor(1.00f, 0.50f, 0.00f) }, // orange
        { TEXT("trunk"),         FLinearColor(1.00f, 0.65f, 0.00f) }, // amber
        { TEXT("primary"),       FLinearColor(1.00f, 0.85f, 0.00f) }, // yellow
        { TEXT("secondary"),     FLinearColor(0.60f, 0.85f, 0.20f) }, // yellow-green
        { TEXT("tertiary"),      FLinearColor(0.40f, 0.75f, 0.40f) }, // green
        { TEXT("unclassified"),  FLinearColor(0.70f, 0.70f, 0.70f) }, // grey
        { TEXT("residential"),   FLinearColor(0.85f, 0.85f, 0.85f) }, // light grey
    };

    const FLinearColor DefaultRoadColor   = FLinearColor(0.8f, 0.8f, 0.8f);
    const FLinearColor ForestColor        = FLinearColor(0.2f, 0.7f, 0.2f);
    const FLinearColor WaterColor         = FLinearColor(0.2f, 0.5f, 1.0f);

    // Parent actors (act as folders in the outliner)
    AActor* RoadParent   = Params.bBuildRoads   ? SpawnParentActor(World, TEXT("GeoTerrain_Roads"))   : nullptr;
    AActor* ForestParent = Params.bBuildForests  ? SpawnParentActor(World, TEXT("GeoTerrain_Forests")) : nullptr;
    AActor* WaterParent  = Params.bBuildWater    ? SpawnParentActor(World, TEXT("GeoTerrain_Water"))   : nullptr;

    int32 RoadIdx = 0, ForestIdx = 0, WaterIdx = 0;

    for (const FOSMWay& Way : Params.OSM->Ways)
    {
        // Build world-space point list
        TArray<FVector> Points;
        Points.Reserve(Way.NodeIds.Num());
        for (int64 NodeId : Way.NodeIds)
        {
            const FOSMNode* Node = Params.OSM->Nodes.Find(NodeId);
            if (!Node) continue;

            // Skip nodes outside our bbox
            if (Node->Lat < Params.Grid->LatMin || Node->Lat > Params.Grid->LatMax ||
                Node->Lon < Params.Grid->LonMin || Node->Lon > Params.Grid->LonMax)
                continue;

            Points.Add(LatLonToWorld(Node->Lat, Node->Lon,
                                     *Params.Grid, Params.XYScaleCM, Params.RoadOffsetCM));
        }

        if (Points.Num() < 2) continue;

        Points = SimplifyPath(Points, 200.f); // merge points < 2m apart

        switch (Way.Type)
        {
        case EOSMWayType::Road:
            if (!Params.bBuildRoads) continue;
            if (RoadIdx >= Params.MaxWaysPerType) continue;
            {
                const FLinearColor* Color = HighwayColors.Find(Way.HighwayClass);
                const FName Label(*FString::Printf(TEXT("Road_%lld"), Way.Id));
                SpawnSplineActor(World, Points, Label,
                                 Color ? *Color : DefaultRoadColor, RoadParent);
                ++RoadIdx;
            }
            break;

        case EOSMWayType::Forest:
            if (!Params.bBuildForests) continue;
            if (ForestIdx >= Params.MaxWaysPerType) continue;
            {
                const FName Label(*FString::Printf(TEXT("Forest_%lld"), Way.Id));
                SpawnSplineActor(World, Points, Label, ForestColor, ForestParent);
                ++ForestIdx;
            }
            break;

        case EOSMWayType::Water:
            if (!Params.bBuildWater) continue;
            if (WaterIdx >= Params.MaxWaysPerType) continue;
            {
                const FName Label(*FString::Printf(TEXT("Water_%lld"), Way.Id));
                SpawnSplineActor(World, Points, Label, WaterColor, WaterParent);
                ++WaterIdx;
            }
            break;

        default:
            break;
        }
    }

    World->MarkPackageDirty();

    Result.RoadsBuilt   = RoadIdx;
    Result.ForestsBuilt = ForestIdx;
    Result.WaterBuilt   = WaterIdx;
    Result.bSuccess     = true;
    return Result;
}
