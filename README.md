# GeoTerrain — Unreal Engine 5.6 Plugin

Generate real-world terrain in Unreal Engine 5.6 from a geographic bounding box: topographic heightmaps, automatic material layers, OpenStreetMap roads, and procedural vegetation — all from a single editor panel.

## Features

| Stage | What it does | Data source |
|-------|-------------|-------------|
| **1. Elevation** | Downloads elevation tiles, stitches + crops + bilinear-resamples to 1009×1009 | [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Terrarium RGB-PNG, no API key) |
| **2. Landscape** | Spawns an `ALandscape` with real-world geographic scale (cm/unit) | UE5 Landscape API |
| **3. Roads & land-use** | Fetches roads, forests and water; builds `USplineComponent` actors | [OpenStreetMap Overpass API](https://overpass-api.de/) |
| **4. Materials & foliage** | Auto-paints Snow/Rock/Grass/Dirt layers by altitude & slope; scatters foliage instances avoiding roads | `FLandscapeEditDataInterface` + `AInstancedFoliageActor` |

## Installation

1. Copy the `GeoTerrain` folder into your project's `Plugins/` directory:
   ```
   YourProject/Plugins/GeoTerrain/
   ```
2. Open the project in **Unreal Engine 5.6** and accept the prompt to rebuild.
3. Open the panel via **Window → GeoTerrain**.

## Usage

1. Enter a WGS84 bounding box (decimal degrees): Lat Min/Max, Lon Min/Max.
2. Toggle the OpenStreetMap layers, material auto-painting and vegetation options.
3. Press **Generate Terrain**. Progress is shown in the panel.

The result appears in the level outliner:

```
GeoTerrain_Landscape     ← heightmap with real-world scale
GeoTerrain_Roads/        ← color-coded road splines
GeoTerrain_Forests/      ← woodland splines
GeoTerrain_Water/        ← water-body splines
```

## Notes

- For material layers to be visible, assign a `LandscapeMaterial` that uses a
  `LandscapeLayerBlend` node with the layer names `Snow`, `Rock`, `Grass`, `Dirt`.
- Vegetation uses the engine placeholder cone mesh by default — replace it with
  your own tree meshes in the UE5 Foliage panel.
- Elevation tiles are at zoom 10 (~300 m/px); good for regional terrain.

## Architecture

```
Source/GeoTerrainEditor/
├── UI/GeoTerrainPanel            Slate panel + pipeline orchestration
├── DEM/CopernicusDEMFetcher      Elevation tile download & decode
├── OSM/OSMDataFetcher            Overpass API query & JSON parsing
└── Terrain/
    ├── LandscapeBuilder          Heightmap → ALandscape
    ├── RoadSplineBuilder         OSM ways → spline actors
    ├── MaterialApplicator        Altitude/slope → weight maps
    └── FoliagePlacer             Procedural foliage scatter
```

## Status

Beta. The UE5 APIs for landscape import, foliage instancing and layer painting
vary between engine versions — if compilation fails on your exact version,
the most likely culprits are `ALandscape::Import`, `AInstancedFoliageActor::AddMesh`
and `FLandscapeEditDataInterface::SetAlphaData`.

## License

MIT
