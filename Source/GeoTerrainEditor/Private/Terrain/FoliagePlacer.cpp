#include "Terrain/FoliagePlacer.h"
#include "InstancedFoliageActor.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

    // ── Get / create IFA ─────────────────────────────────────────────────────
    AInstancedFoliageActor* IFA =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, /*CreateIfNone=*/true);

    if (!IFA) { Result.Error = TEXT("Failed to get InstancedFoliageActor."); return Result; }

    // UE5.6 API: AddMesh asserts the mesh is NOT already a foliage source.
    // On a re-run the mesh is already registered, so reuse it instead of
    // calling AddMesh again (which would trip the assertion and crash).
    UFoliageType* FoliageTypePtr = nullptr;
    FFoliageInfo* FoliageInfo    = nullptr;

    FoliageTypePtr = IFA->GetLocalFoliageTypeForSource(TreeMesh, &FoliageInfo);
    if (!FoliageTypePtr || !FoliageInfo)
    {
        FoliageInfo    = IFA->AddMesh(TreeMesh, &FoliageTypePtr);
    }

    if (!FoliageTypePtr || !FoliageInfo)
    {
        Result.Error = TEXT("Failed to add mesh to InstancedFoliageActor.");
        return Result;
    }

    // ── Placement loop ───────────────────────────────────────────────────────
    const float CellSizeM     = XYScaleCM / 100.f;
    const float ExclusionCM   = Rules.RoadExclusionM * 100.f;
    const float DensityFrac   = FMath::Clamp(Rules.DensityPct / 100.f, 0.001f, 1.f);
    const int32 Stride        = FMath::Max(1, FMath::RoundToInt(1.f / FMath::Sqrt(DensityFrac)));

    FRandomStream Rng(12345); // deterministic seed

    TArray<FFoliageInstance> NewInstances;
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

            FFoliageInstance Inst;
            Inst.Location     = FVector(WX, WY, WZ);
            Inst.Rotation     = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
            // FFoliageInstance::DrawScale3D is FVector3f in UE5.6
            Inst.DrawScale3D  = FVector3f(Rng.FRandRange(Rules.ScaleMin, Rules.ScaleMax));
            NewInstances.Add(Inst);
        }
    }

    if (NewInstances.Num() == 0)
    {
        Result.Error = TEXT("No valid placement positions found with current rules.");
        return Result;
    }

    // Add all instances in one batch
    TArray<const FFoliageInstance*> InstancePtrs;
    InstancePtrs.Reserve(NewInstances.Num());
    for (const FFoliageInstance& Inst : NewInstances)
        InstancePtrs.Add(&Inst);

    FoliageInfo->AddInstances(FoliageTypePtr, InstancePtrs);

    World->MarkPackageDirty();

    Result.InstancesPlaced = NewInstances.Num();
    Result.bSuccess        = true;
    return Result;
}
