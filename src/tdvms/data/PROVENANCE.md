# tdvms catalogs

Copied from tdvms-pull (`src/tdvms/data/`, github.com/hogib/tdvms-pull) on
2026-11-09, unchanged, and embedded as Qt resources at `:/tdvms/data/`.

| file | contents | source |
|---|---|---|
| `stations.csv` | every station the TDVMS portal lists: code, network, latitude, longitude, instruments (of H, L, N) | the TDVMS portal, `GetStations` over TU, KO, TB, TK |
| `events.csv` | M5+ earthquakes since 1990 over the station box (34.5–43 N, 25–45.5 E) | USGS FDSN event service, public domain |

Refresh with the Catalog refresh action of the tdvms module (or
`python -m tdvms.catalog` in tdvms-pull); a refresh is a reviewable diff.
