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
#include "Widgets/Input/SComboBox.h"
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
    // Valid landscape sizes (N where (N-1) is a multiple of 126).
    MapSizeOptions.Empty();
    MapSizeOptions.Add(MakeShared<FString>(TEXT("505 (rápido, baja)")));
    MapSizeOptions.Add(MakeShared<FString>(TEXT("1009 (medio)")));
    MapSizeOptions.Add(MakeShared<FString>(TEXT("2017 (alto — recomendado)")));
    MapSizeOptions.Add(MakeShared<FString>(TEXT("4033 (máximo — pesado)")));
    SelectedMapSize = MapSizeOptions[2]; // 2017 default

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
                .Text(LOCTEXT("Sub","Generador de terreno real — UE 5.8"))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.55f,0.55f,0.55f)))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── [1] Punto central + extensión ─────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("BB","[1] Punto central (WGS84)  —  será el centro (0,0) del terreno"))
                .Font(FAppStyle::GetFontStyle("SmallText"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                MakeCoordRow(
                    LOCTEXT("CLat","Latitud"), CenterLatBox,
                    LOCTEXT("CLon","Longitud"), CenterLonBox,
                    40.75f, -4.00f)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [ SNew(STextBlock).Text(LOCTEXT("Ext","Lado del terreno (km):")) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SAssignNew(ExtentKmBox, SEditableTextBox).Text(FText::FromString(TEXT("2.0"))) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(UseIGNBox, SCheckBox).IsChecked(ECheckBoxState::Checked) ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4,0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("UseIGN","Source: IGN MDT 5 m  (Spain, max resolution — uncheck for global SRTM)"))
                    .AutoWrapText(true)
                ]
            ]
            // Map size selector
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [ SNew(STextBlock).Text(LOCTEXT("MapSize","Map size (resolution):")) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&MapSizeOptions)
                    .InitiallySelectedItem(SelectedMapSize)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
                    {
                        return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString()));
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSel, ESelectInfo::Type)
                    {
                        if (NewSel.IsValid()) { SelectedMapSize = NewSel; }
                    })
                    [
                        SNew(STextBlock).Text_Lambda([this]()
                        {
                            return FText::FromString(SelectedMapSize.IsValid() ? *SelectedMapSize : TEXT("2017"));
                        })
                    ]
                ]
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

            // ── [3] Landscape material ────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("LMatHdr","[3] Landscape material  (object path — Auto-Landscape recommended)"))
                .Font(FAppStyle::GetFontStyle("SmallText"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0,0,14)
            [
                SAssignNew(LandscapeMaterialBox, SEditableTextBox)
                .Text(LOCTEXT("LMatDefault",
                    "/Game/Hyper/Environment_Building/AutoLandscape/BaseMaterial/Instance/Config_Examples/Edits/MI_AutoLandscape_Forest_Snow_LG_Rocks.MI_AutoLandscape_Forest_Snow_LG_Rocks"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [ SNew(SSeparator) ]

            // ── [3b] Weightmap layers (only for layer-based materials) ────
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SAssignNew(PaintLayersBox, SCheckBox).IsChecked(ECheckBoxState::Unchecked) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("MatHdr","Auto-paint weightmap layers  (Snow/Rock/Grass/Dirt — only for layer materials)"))
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
        SetStatus(TEXT("Coordenadas inválidas: lat [-90,90], lon [-180,180] y lado > 0 km."), true);
        return FReply::Handled();
    }

    bGenerating      = true;
    CachedGrid       = FElevationGrid{};
    CachedOSMData    = FOSMData{};
    CachedLandscape  = nullptr;

    ProgressBar->SetVisibility(EVisibility::Visible);
    ProgressBar->SetPercent(0.f);

    RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(
        this, &SGeoTerrainPanel::TickProgress));

    const bool  bUseIGN = UseIGNBox.IsValid() && UseIGNBox->IsChecked();
    const int32 MapSize = GetSelectedMapSize();
    const FOnElevationReady Cb =
        FOnElevationReady::CreateSP(this, &SGeoTerrainPanel::OnElevationReady);

    if (bUseIGN)
    {
        SetStatus(FString::Printf(
            TEXT("[1/4] Downloading IGN MDT 5 m → %d²  (%.4f,%.4f)–(%.4f,%.4f)..."),
            MapSize, LatMin, LonMin, LatMax, LonMax));
        ActiveIGNFetcher = MakeShared<FIGNDEMFetcher>();
        ActiveIGNFetcher->FetchBBox(LatMin, LatMax, LonMin, LonMax, Cb, MapSize);
    }
    else
    {
        SetStatus(FString::Printf(
            TEXT("[1/4] Downloading global elevation → %d²  (%.4f,%.4f)–(%.4f,%.4f)..."),
            MapSize, LatMin, LonMin, LatMax, LonMax));
        ActiveDEMFetcher = MakeShared<FCopernicusDEMFetcher>();
        ActiveDEMFetcher->FetchBBox(LatMin, LatMax, LonMin, LonMax, Cb, MapSize);
    }

    return FReply::Handled();
}

int32 SGeoTerrainPanel::GetSelectedMapSize() const
{
    // Options start with the size number; Atoi reads the leading digits.
    const int32 Size = SelectedMapSize.IsValid() ? FCString::Atoi(**SelectedMapSize) : 2017;
    return (Size >= 505) ? Size : 2017;
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

    const FString MaterialPath = LandscapeMaterialBox.IsValid()
        ? LandscapeMaterialBox->GetText().ToString().TrimStartAndEnd()
        : FString();
    FLandscapeBuilder::FBuildResult BR = FLandscapeBuilder::Build(Grid, MaterialPath);
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
    ActiveIGNFetcher.Reset();
    ActiveOSMFetcher.Reset();
}

EActiveTimerReturnType SGeoTerrainPanel::TickProgress(double /*t*/, float /*dt*/)
{
    if (!bGenerating) return EActiveTimerReturnType::Stop;
    if (ActiveDEMFetcher.IsValid())
        ProgressBar->SetPercent(ActiveDEMFetcher->GetProgress() * 0.3f);
    else if (ActiveIGNFetcher.IsValid())
        ProgressBar->SetPercent(ActiveIGNFetcher->GetProgress() * 0.3f);
    return EActiveTimerReturnType::Continue;
}

bool SGeoTerrainPanel::ParseCoords(float& LatMin, float& LatMax,
                                    float& LonMin, float& LonMax) const
{
    if (!CenterLatBox.IsValid() || !CenterLonBox.IsValid() || !ExtentKmBox.IsValid())
    {
        return false;
    }

    const float CenterLat = FCString::Atof(*CenterLatBox->GetText().ToString());
    const float CenterLon = FCString::Atof(*CenterLonBox->GetText().ToString());
    const float ExtentKm  = FCString::Atof(*ExtentKmBox->GetText().ToString());

    if (ExtentKm <= 0.f
        || !FMath::IsWithinInclusive(CenterLat, -90.f, 90.f)
        || !FMath::IsWithinInclusive(CenterLon, -180.f, 180.f))
    {
        return false;
    }

    // km -> grados: 1° lat ≈ 111.32 km ; 1° lon ≈ 111.32·cos(lat) km
    const float HalfKm     = ExtentKm * 0.5f;
    const float HalfLatDeg = HalfKm / 111.32f;
    const float CosLat     = FMath::Max(FMath::Cos(FMath::DegreesToRadians(CenterLat)), 0.01f);
    const float HalfLonDeg = HalfKm / (111.32f * CosLat);

    LatMin = CenterLat - HalfLatDeg;  LatMax = CenterLat + HalfLatDeg;
    LonMin = CenterLon - HalfLonDeg;  LonMax = CenterLon + HalfLonDeg;

    return (LatMin < LatMax) && (LonMin < LonMax);
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
