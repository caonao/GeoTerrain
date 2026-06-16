#include "UI/GeoTerrainPanel.h"
#include "DEM/CopernicusDEMFetcher.h"
#include "OSM/OSMDataFetcher.h"
#include "Terrain/LandscapeBuilder.h"
#include "Terrain/RoadSplineBuilder.h"
#include "Terrain/MaterialApplicator.h"
#include "Terrain/FoliagePlacer.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "SGeoTerrainPanel"

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

static FText F2T(float V) { return FText::FromString(FString::Printf(TEXT("%.1f"), V)); }
static FText I2T(int32 V) { return FText::FromString(FString::FromInt(V)); }

TSharedRef<SWidget> SGeoTerrainPanel::MakeCoordRow(
    const FText& LA, TSharedPtr<SEditableTextBox>& BA,
    const FText& LB, TSharedPtr<SEditableTextBox>& BB,
    float DA, float DB)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(0,0,6,0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,2)
            [ SNew(STextBlock).Text(LA) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SAssignNew(BA, SEditableTextBox).Text(FText::FromString(FString::Printf(TEXT("%.4f"), DA))) ]
        ]
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(6,0,0,0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,2)
            [ SNew(STextBlock).Text(LB) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SAssignNew(BB, SEditableTextBox).Text(FText::FromString(FString::Printf(TEXT("%.4f"), DB))) ]
        ];
}

TSharedRef<SWidget> SGeoTerrainPanel::MakeField(const FText& Label,
                                                  TSharedPtr<SEditableTextBox>& Box,
                                                  float Default,
                                                  const FText& Suffix)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
        [ SNew(STextBlock).Text(Label) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ SAssignNew(Box, SEditableTextBox).Text(F2T(Default)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,0,0)
        [ SNew(STextBlock).Text(Suffix).ColorAndOpacity(FSlateColor(FLinearColor(0.55f,0.55f,0.55f))) ];
}

// ---------------------------------------------------------------------------
// Construct — full 4-phase panel
// ---------------------------------------------------------------------------

void SGeoTerrainPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot().Padding(12.f, 10.f)
        [
            SNew(SVerticalBox)

            // ── Header ────────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("T","GeoTerrain"))
                .Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Sub","Real-world terrain generator — UE 5.6"))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.55f,0.55f,0.55f)))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── [1] Bounding box ──────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("BB","[1] Bounding Box  (WGS84 decimal degrees)"))
                .Font(FAppStyle::GetFontStyle("SmallText"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                MakeCoordRow(
                    LOCTEXT("LatMin","Lat Min"), LatMinBox,
                    LOCTEXT("LatMax","Lat Max"), LatMaxBox,
                    40.0f, 41.0f)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
            [
                MakeCoordRow(
                    LOCTEXT("LonMin","Lon Min"), LonMinBox,
                    LOCTEXT("LonMax","Lon Max"), LonMaxBox,
                    -4.0f, -3.0f)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── [2] OSM layers ────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("OSMHdr","[2] OpenStreetMap layers"))
                .Font(FAppStyle::GetFontStyle("SmallText"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0,0,3)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(IncludeRoadsBox, SCheckBox).IsChecked(ECheckBoxState::Checked) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [ SNew(STextBlock).Text(LOCTEXT("Roads","Roads  (motor → residential)")) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0,0,3)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(IncludeForestsBox, SCheckBox).IsChecked(ECheckBoxState::Checked) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [ SNew(STextBlock).Text(LOCTEXT("Forests","Forest / woodland areas")) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0,0,14)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(IncludeWaterBox, SCheckBox).IsChecked(ECheckBoxState::Checked) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [ SNew(STextBlock).Text(LOCTEXT("Water","Water bodies / rivers")) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── [3] Material layers ───────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(PaintLayersBox, SCheckBox).IsChecked(ECheckBoxState::Checked) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("MatHdr","[3] Auto-paint material layers  (Snow / Rock / Grass / Dirt)"))
                    .Font(FAppStyle::GetFontStyle("SmallText"))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2,0,2)
            [ MakeField(LOCTEXT("Snow","Snow above"), SnowAltBox, 2000.f, LOCTEXT("m","m")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2,0,2)
            [ MakeField(LOCTEXT("Rock","Rock above"), RockAltBox, 1200.f, LOCTEXT("m2","m")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2,0,14)
            [ MakeField(LOCTEXT("RSlope","Rock slope >"), RockSlopeBox, 40.f, LOCTEXT("deg","°")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── [4] Vegetation ────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(PlaceFoliageBox, SCheckBox).IsChecked(ECheckBoxState::Checked) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("VegHdr","[4] Place vegetation  (placeholder cone mesh)"))
                    .Font(FAppStyle::GetFontStyle("SmallText"))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2,0,2)
            [ MakeField(LOCTEXT("TreeLine","Tree line"), TreeLinBox, 2500.f, LOCTEXT("m3","m")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2,0,2)
            [ MakeField(LOCTEXT("Density","Density"), FoliageDensityBox, 20.f, LOCTEXT("pct","% of eligible pixels")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2,0,14)
            [ MakeField(LOCTEXT("MaxInst","Max trees"), MaxInstancesBox, 20000.f, FText::GetEmpty()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── Generate button ───────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("Gen","Generate Terrain"))
                .IsEnabled_Lambda([this]() { return !bGenerating; })
                .OnClicked(this, &SGeoTerrainPanel::OnGenerateClicked)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SAssignNew(ProgressBar, SProgressBar)
                .Percent(0.f)
                .Visibility(EVisibility::Hidden)
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SAssignNew(StatusText, STextBlock)
                .Text(LOCTEXT("Ready","Ready — configure options and press Generate."))
                .AutoWrapText(true)
                .ColorAndOpacity(FSlateColor(FLinearColor(0.55f,0.55f,0.55f)))
            ]
        ]
    ];
}

// ---------------------------------------------------------------------------
// Pipeline — Stage 1
// ---------------------------------------------------------------------------

FReply SGeoTerrainPanel::OnGenerateClicked()
{
    float LatMin, LatMax, LonMin, LonMax;
    if (!ParseCoords(LatMin, LatMax, LonMin, LonMax))
    {
        SetStatus(TEXT("Invalid coordinates: LatMin < LatMax and LonMin < LonMax required."), true);
        return FReply::Handled();
    }

    bGenerating      = true;
    CachedGrid       = FElevationGrid{};
    CachedOSMData    = FOSMData{};
    CachedLandscape  = nullptr;

    ProgressBar->SetVisibility(EVisibility::Visible);
    ProgressBar->SetPercent(0.f);
    SetStatus(FString::Printf(
        TEXT("[1/4] Downloading elevation tiles (%.4f,%.4f)–(%.4f,%.4f)..."),
        LatMin, LonMin, LatMax, LonMax));

    RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(
        this, &SGeoTerrainPanel::TickProgress));

    ActiveDEMFetcher = MakeShared<FCopernicusDEMFetcher>();
    ActiveDEMFetcher->FetchBBox(LatMin, LatMax, LonMin, LonMax,
        FOnElevationReady::CreateSP(this, &SGeoTerrainPanel::OnElevationReady), 1009);

    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Pipeline — Stage 2: elevation ready → landscape
// ---------------------------------------------------------------------------

void SGeoTerrainPanel::OnElevationReady(const FElevationGrid& Grid)
{
    if (!Grid.bValid)
    {
        SetStatus(FString::Printf(TEXT("Elevation failed: %s"), *Grid.Error), true);
        FinishPipeline();
        return;
    }

    CachedGrid = Grid;
    ProgressBar->SetPercent(0.3f);
    SetStatus(FString::Printf(
        TEXT("[2/4] Building landscape (%dx%d, %.0f–%.0f m)..."),
        Grid.Width, Grid.Height, Grid.ElevMin, Grid.ElevMax));

    FLandscapeBuilder::FBuildResult BR = FLandscapeBuilder::Build(Grid);
    if (!BR.bSuccess)
    {
        SetStatus(FString::Printf(TEXT("Landscape failed: %s"), *BR.Error), true);
        FinishPipeline();
        return;
    }

    CachedXYScale   = BR.XYScaleCM;
    CachedLandscape = BR.Landscape;

    const bool bWantsOSM =
        (IncludeRoadsBox.IsValid()   && IncludeRoadsBox->IsChecked())   ||
        (IncludeForestsBox.IsValid() && IncludeForestsBox->IsChecked()) ||
        (IncludeWaterBox.IsValid()   && IncludeWaterBox->IsChecked());

    if (bWantsOSM)
    {
        ProgressBar->SetPercent(0.45f);
        SetStatus(TEXT("[3/4] Fetching OpenStreetMap data..."));
        ActiveOSMFetcher = MakeShared<FOSMDataFetcher>();
        ActiveOSMFetcher->FetchBBox(Grid.LatMin, Grid.LatMax, Grid.LonMin, Grid.LonMax,
            FOnOSMReady::CreateSP(this, &SGeoTerrainPanel::OnOSMReady));
    }
    else
    {
        RunMaterialAndFoliagePhase();
    }
}

// ---------------------------------------------------------------------------
// Pipeline — Stage 3: OSM → splines
// ---------------------------------------------------------------------------

void SGeoTerrainPanel::OnOSMReady(const FOSMData& OSMData)
{
    CachedOSMData = OSMData;

    if (OSMData.bValid)
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            FRoadSplineBuilder::FBuildParams P;
            P.OSM           = &CachedOSMData;
            P.Grid          = &CachedGrid;
            P.XYScaleCM     = CachedXYScale;
            P.bBuildRoads   = IncludeRoadsBox.IsValid()   && IncludeRoadsBox->IsChecked();
            P.bBuildForests = IncludeForestsBox.IsValid() && IncludeForestsBox->IsChecked();
            P.bBuildWater   = IncludeWaterBox.IsValid()   && IncludeWaterBox->IsChecked();
            FRoadSplineBuilder::Build(P, World);
        }
    }
    else
    {
        SetStatus(FString::Printf(TEXT("OSM failed: %s  (continuing…)"), *OSMData.Error), true);
    }

    RunMaterialAndFoliagePhase();
}

// ---------------------------------------------------------------------------
// Pipeline — Stage 4: material layers + foliage
// ---------------------------------------------------------------------------

void SGeoTerrainPanel::RunMaterialAndFoliagePhase()
{
    ProgressBar->SetPercent(0.75f);

    FString Log;

    // Material layers
    if (PaintLayersBox.IsValid() && PaintLayersBox->IsChecked() && CachedLandscape)
    {
        SetStatus(TEXT("[4/4] Painting material layers..."));

        FMaterialRules MR;
        MR.SnowAltitudeM = GetBoxFloat(SnowAltBox,   2000.f);
        MR.RockAltitudeM = GetBoxFloat(RockAltBox,   1200.f);
        MR.RockSlopeDeg  = GetBoxFloat(RockSlopeBox,   40.f);

        FMaterialApplicator::FApplyResult AR =
            FMaterialApplicator::Apply(CachedLandscape, CachedGrid, MR, CachedXYScale);

        if (AR.bSuccess)
            Log += FString::Printf(TEXT("Layers: Snow %d  Rock %d  Grass %d  Dirt %d\n"),
                                   AR.SnowPixels, AR.RockPixels, AR.GrassPixels, AR.DirtPixels);
        else
            Log += FString::Printf(TEXT("Material layers failed: %s\n"), *AR.Error);
    }

    // Foliage
    if (PlaceFoliageBox.IsValid() && PlaceFoliageBox->IsChecked())
    {
        SetStatus(TEXT("[4/4] Placing vegetation..."));

        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            FFoliageRules FR;
            FR.TreeLineAltitudeM = GetBoxFloat(TreeLinBox,        2500.f);
            FR.DensityPct        = GetBoxFloat(FoliageDensityBox,   20.f);
            FR.MaxInstances      = GetBoxInt  (MaxInstancesBox,    20000);

            const FOSMData* OSMPtr = CachedOSMData.bValid ? &CachedOSMData : nullptr;

            FFoliagePlacer::FPlaceResult PR =
                FFoliagePlacer::Place(CachedGrid, CachedXYScale, FR, OSMPtr, nullptr, World);

            if (PR.bSuccess)
                Log += FString::Printf(TEXT("Trees placed: %d"), PR.InstancesPlaced);
            else
                Log += FString::Printf(TEXT("Foliage failed: %s"), *PR.Error);
        }
    }

    ProgressBar->SetPercent(1.f);

    const FString Summary = FString::Printf(
        TEXT("Done!  Elev: %.0f–%.0f m  |  XY: %.0f cm/u  |  %s"),
        CachedGrid.ElevMin, CachedGrid.ElevMax, CachedXYScale, *Log);
    SetStatus(Summary);

    FinishPipeline();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void SGeoTerrainPanel::FinishPipeline()
{
    bGenerating = false;
    ProgressBar->SetVisibility(EVisibility::Hidden);
    ActiveDEMFetcher.Reset();
    ActiveOSMFetcher.Reset();
}

EActiveTimerReturnType SGeoTerrainPanel::TickProgress(double /*t*/, float /*dt*/)
{
    if (!bGenerating) return EActiveTimerReturnType::Stop;
    if (ActiveDEMFetcher.IsValid())
        ProgressBar->SetPercent(ActiveDEMFetcher->GetProgress() * 0.3f);
    return EActiveTimerReturnType::Continue;
}

bool SGeoTerrainPanel::ParseCoords(float& LatMin, float& LatMax,
                                    float& LonMin, float& LonMax) const
{
    if (!LatMinBox.IsValid() || !LatMaxBox.IsValid() || !LonMinBox.IsValid() || !LonMaxBox.IsValid())
        return false;

    LatMin = FCString::Atof(*LatMinBox->GetText().ToString());
    LatMax = FCString::Atof(*LatMaxBox->GetText().ToString());
    LonMin = FCString::Atof(*LonMinBox->GetText().ToString());
    LonMax = FCString::Atof(*LonMaxBox->GetText().ToString());

    return (LatMin < LatMax) && (LonMin < LonMax)
        && FMath::IsWithinInclusive(LatMin, -90.f, 90.f)
        && FMath::IsWithinInclusive(LatMax, -90.f, 90.f)
        && FMath::IsWithinInclusive(LonMin, -180.f, 180.f)
        && FMath::IsWithinInclusive(LonMax, -180.f, 180.f);
}

float SGeoTerrainPanel::GetBoxFloat(const TSharedPtr<SEditableTextBox>& Box, float Default) const
{
    return Box.IsValid() ? FCString::Atof(*Box->GetText().ToString()) : Default;
}

int32 SGeoTerrainPanel::GetBoxInt(const TSharedPtr<SEditableTextBox>& Box, int32 Default) const
{
    return Box.IsValid() ? FCString::Atoi(*Box->GetText().ToString()) : Default;
}

void SGeoTerrainPanel::SetStatus(const FString& Message, bool bError)
{
    if (!StatusText.IsValid()) return;
    StatusText->SetText(FText::FromString(Message));
    StatusText->SetColorAndOpacity(bError
        ? FSlateColor(FLinearColor(1.0f, 0.35f, 0.35f))
        : FSlateColor(FLinearColor(0.4f, 0.9f, 0.4f)));
}

#undef LOCTEXT_NAMESPACE
