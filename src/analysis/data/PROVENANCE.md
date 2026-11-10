# Data embedded with the analysis library

- `countries.geojson`: Turkey and its neighbours, the background of the
  Regions maps. Made from Natural Earth, 1:10m Cultural Vectors, "Admin 0 –
  Countries", the point-of-view file `ne_10m_admin_0_countries_tur` (the
  one the original script, earthquake-clustering-eafz, reads), version
  5.1.1.
  - Source: https://naciscdn.org/naturalearth/10m/cultural/ne_10m_admin_0_countries_tur.zip
    (https://www.naturalearthdata.com/downloads/10m-cultural-vectors/),
    fetched 2026-11-10, sha256
    `2019fd24f5ac8dc68b90a2f26e236e5e1f5bd97b75e7e88520c3f20ac22e7fde`.
  - Licence: public domain (https://www.naturalearthdata.com/about/terms-of-use/).
    "Made with Natural Earth."
  - Changes: `packaging/naturalearth/clip_countries.py` (pyshp 2.3.1,
    shapely 2.0.6) keeps each country's part inside longitude 24–48°E,
    latitude 32–44°N, simplifies it (Douglas-Peucker, 0.004°), rounds the
    coordinates to 0.001° and keeps only the `NAME` and `SOVEREIGNT`
    attributes (as `name`, `sovereignt`). 21 features, 111 kB, sha256
    `02596d561d4070e07f598a44e8d6956ef8e63c64347f0cb4f01df8eb49e38b8a`.
  - The boundaries are Natural Earth's; the maps print the note that map
    lines do not necessarily depict accepted national boundaries.
