#include "Terrain/FoliagePlacer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Math/RandomStream.h"
#include "UObject/UObjectGlobals.h"

// ---------------------------------------------------------------------------
// Slope helper (shared with MaterialApplicator — kept local to avoid coupling)
// ---------------------------------------------------------------------------

float FFoliagePlacer::ComputeSlopeDeg(const FElevationGrid& Grid,
                                       int32 Row, int32 Col, float CellSizeM)
{
    const float H = Grid.Data[Row * Grid.Width + Col];
    const float L = (Col > 0)              ? Grid.Data[Row * Grid.Width + Col - 1] : H;
    const float R = (Col < Grid.Width - 1) ? Grid.Data[Row * Grid.Width + Col + 1] : H;
    const float U = (Row > 0)              ? Grid.Data[(Row - 1) * Grid.Width + Col] : H;
    const float D = (Row < Grid.Height- 1) ? Grid.Data[(Row + 1) * Grid.Width + Col] : H;
    const float dX = (R - L) / (2.f * CellSizeM);
    const float dY = (D - U) / (2.f * CellSizeM);
    return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(dX * dX + dY * dY)));
}

// ---------------------------------------------------------------------------
// Road exclusion check (world XY in cm)
// ---------------------------------------------------------------------------

bool FFoliagePlacer::IsNearRoad(const FVector2D& WorldXY, const FOSMData& OSMData,
                                  const FElevationGrid& Grid, float XYScaleCM,
                                  float ExclusionDistCM)
{
    const float ExcSq = ExclusionDistCM * ExclusionDistCM;

    for (const FOSMWay& Way : OSMData.Ways)
    {
        if (Way.Type != EOSMWayType::Road) continue;

        for (int64 NodeId : Way.NodeIds)
        {
            const FOSMNode* Node = OSMData.Nodes.Find(NodeId);
            if (!Node) continue;

            const float NX = (float)((Node->Lon - Grid.LonMin) / (Grid.LonMax - Grid.LonMin));
            const float NY = (float)((Grid.LatMax - Node->Lat) / (Grid.LatMax - Grid.LatMin));

            const FVector2D NodeWorld(
                NX * (Grid.Width  - 1) * XYScaleCM,
                NY * (Grid.Height - 1) * XYScaleCM);

            if (FVector2D::DistSquared(WorldXY, NodeWorld) < ExcSq)
                return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Main placement
// ---------------------------------------------------------------------------

FFoliagePlacer::FPlaceResult FFoliagePlacer::Place(const FElevationGrid& Grid,
                                                    float XYScaleCM,
                                                    const FFoliageRules& Rules,
                                                    const FOSMData* OSMData,
                                                    UStaticMesh* InMesh,
                                                    UWorld* World)
{
    FPlaceResult Result;

    if (!Grid.bValid)  { Result.Error = TEXT("Invalid elevation grid."); return Result; }
    if (!World)        { Result.Error = TEXT("Null world.");              return Result; }

    // ── Resolve mesh ─────────────────────────────────────────────────────────
    UStaticMesh* TreeMesh = InMesh;
    if (!TreeMesh)
    {
        TreeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
        if (!TreeMesh)
        {
            Result.Error = TEXT("Could not load placeholder cone mesh.");
            return Result;
        }
    }

    // ── Get / create HISM container actor ────────────────────────────────────
    // UE 5.8: the editor foliage path (IFA + FFoliageInfo::AddInstances) crashes
    // with World Partition worlds. We place instances on our own HISM instead —
    // same runtime representation PCG uses, stable across engine versions.
    AActor* Container = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->ActorHasTag(TEXT("GeoTerrainFoliage"))) { Container = *It; break; }
    }
    if (!Container)
    {
        FActorSpawnParameters SP;
        SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Container = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SP);
        if (!Container) { Result.Error = TEXT("Failed to spawn foliage container actor."); return Result; }
        Container->Tags.Add(TEXT("GeoTerrainFoliage"));
#if WITH_EDITOR
        Container->SetActorLabel(TEXT("GeoTerrainFoliage"));
#endif
        USceneComponent* RootC = NewObject<USceneComponent>(Container, TEXT("Root"));
        Container->SetRootComponent(RootC);
        RootC->RegisterComponent();
        Container->AddInstanceComponent(RootC);
    }

    UHierarchicalInstancedStaticMeshComponent* HISM = nullptr;
    {
        TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
        Container->GetComponents(Comps);
        for (UHierarchicalInstancedStaticMeshComponent* C : Comps)
        {
            if (C && C->GetStaticMesh() == TreeMesh) { HISM = C; break; }
        }
    }
    if (!HISM)
    {
        HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Container);
        HISM->SetStaticMesh(TreeMesh);
        HISM->SetMobility(EComponentMobility::Static);
        if (Container->GetRootComponent())
        {
            HISM->AttachToComponent(Container->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        }
        HISM->RegisterComponent();
        Container->AddInstanceComponent(HISM);
    }

    // ── Placement loop ───────────────────────────────────────────────────────
    const float CellSizeM     = XYScaleCM / 100.f;
    const float ExclusionCM   = Rules.RoadExclusionM * 100.f;
    const float DensityFrac   = FMath::Clamp(Rules.DensityPct / 100.f, 0.001f, 1.f);
    const int32 Stride        = FMath::Max(1, FMath::RoundToInt(1.f / FMath::Sqrt(DensityFrac)));

    FRandomStream Rng(12345); // deterministic seed

    TArray<FTransform> NewInstances;
    NewInstances.Reserve(FMath::Min(Rules.MaxInstances, (Grid.Width / Stride) * (Grid.Height / Stride)));

    for (int32 Row = 0; Row < Grid.Height && NewInstances.Num() < Rules.MaxInstances; Row += Stride)
    {
        for (int32 Col = 0; Col < Grid.Width && NewInstances.Num() < Rules.MaxInstances; Col += Stride)
        {
            const float Elev  = Grid.Data[Row * Grid.Width + Col];
            if (Elev < Rules.MinAltitudeM)      continue;
            if (Elev > Rules.TreeLineAltitudeM) continue;

            const float Slope = ComputeSlopeDeg(Grid, Row, Col, CellSizeM);
            if (Slope > Rules.MaxSlopeDeg)      continue;

            // World XY position with random sub-cell jitter
            const float JX = Rng.FRandRange(-0.4f, 0.4f) * XYScaleCM;
            const float JY = Rng.FRandRange(-0.4f, 0.4f) * XYScaleCM;
            const float WX = Col * XYScaleCM + JX;
            const float WY = Row * XYScaleCM + JY;

            // Road exclusion (only if OSM data available)
            if (OSMData && OSMData->bValid && OSMData->RoadCount > 0)
            {
                if (IsNearRoad(FVector2D(WX, WY), *OSMData, Grid, XYScaleCM, ExclusionCM))
                    continue;
            }

            // World Z: elevation_m * 100 (same as landscape surface)
            const float WZ = Elev * 100.f;

            const float S = Rng.FRandRange(Rules.ScaleMin, Rules.ScaleMax);
            NewInstances.Emplace(
                FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f),
                FVector(WX, WY, WZ),
                FVector(S));
        }
    }

    if (NewInstances.Num() == 0)
    {
        Result.Error = TEXT("No valid placement positions found with current rules.");
        return Result;
    }

    // Add all instances in one batch (world space)
    HISM->AddInstances(NewInstances, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
    HISM->MarkRenderStateDirty();
    Container->MarkPackageDirty();

    Result.InstancesPlaced = NewInstances.Num();
    Result.bSuccess        = true;
    return Result;
}
