#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "DEM/CopernicusDEMFetcher.h"
#include "OSM/OSMDataFetcher.h"
#include "Terrain/MaterialApplicator.h"
#include "Terrain/FoliagePlacer.h"

class SEditableTextBox;
class STextBlock;
class SProgressBar;
class SCheckBox;

class SGeoTerrainPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGeoTerrainPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // ── Bounding box ──────────────────────────────────────────────────────────
    TSharedPtr<SEditableTextBox> LatMinBox;
    TSharedPtr<SEditableTextBox> LatMaxBox;
    TSharedPtr<SEditableTextBox> LonMinBox;
    TSharedPtr<SEditableTextBox> LonMaxBox;

    // ── OSM options ───────────────────────────────────────────────────────────
    TSharedPtr<SCheckBox> IncludeRoadsBox;
    TSharedPtr<SCheckBox> IncludeForestsBox;
    TSharedPtr<SCheckBox> IncludeWaterBox;

    // ── Material options ──────────────────────────────────────────────────────
    TSharedPtr<SCheckBox>        PaintLayersBox;
    TSharedPtr<SEditableTextBox> SnowAltBox;
    TSharedPtr<SEditableTextBox> RockAltBox;
    TSharedPtr<SEditableTextBox> RockSlopeBox;

    // ── Vegetation options ────────────────────────────────────────────────────
    TSharedPtr<SCheckBox>        PlaceFoliageBox;
    TSharedPtr<SEditableTextBox> TreeLinBox;       // tree-line altitude
    TSharedPtr<SEditableTextBox> FoliageDensityBox;
    TSharedPtr<SEditableTextBox> MaxInstancesBox;

    // ── Feedback ──────────────────────────────────────────────────────────────
    TSharedPtr<STextBlock>   StatusText;
    TSharedPtr<SProgressBar> ProgressBar;

    // ── Pipeline state ────────────────────────────────────────────────────────
    bool bGenerating = false;
    TSharedPtr<FCopernicusDEMFetcher> ActiveDEMFetcher;
    TSharedPtr<FOSMDataFetcher>       ActiveOSMFetcher;

    FElevationGrid CachedGrid;
    float          CachedXYScale    = 100.f;
    ALandscape*    CachedLandscape  = nullptr;
    FOSMData       CachedOSMData;

    // ── Pipeline callbacks ────────────────────────────────────────────────────
    FReply OnGenerateClicked();
    void   OnElevationReady(const FElevationGrid& Grid);
    void   OnOSMReady(const FOSMData& OSMData);
    void   RunMaterialAndFoliagePhase();
    void   FinishPipeline();

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool   ParseCoords(float& OutLatMin, float& OutLatMax,
                       float& OutLonMin, float& OutLonMax) const;
    float  GetBoxFloat(const TSharedPtr<SEditableTextBox>& Box, float Default) const;
    int32  GetBoxInt  (const TSharedPtr<SEditableTextBox>& Box, int32  Default) const;
    void   SetStatus(const FString& Message, bool bError = false);

    EActiveTimerReturnType TickProgress(double InCurrentTime, float InDeltaTime);

    static TSharedRef<SWidget> MakeCoordRow(
        const FText& LabelA, TSharedPtr<SEditableTextBox>& BoxA,
        const FText& LabelB, TSharedPtr<SEditableTextBox>& BoxB,
        float DefaultA, float DefaultB);

    static TSharedRef<SWidget> MakeField(const FText& Label,
                                          TSharedPtr<SEditableTextBox>& Box,
                                          float Default,
                                          const FText& Suffix = FText::GetEmpty());
};
