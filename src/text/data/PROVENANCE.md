# Place names

`places.tsv` lists the populated places between 24–48° E and 32–44° N whose
names hold a letter outside ASCII: name, latitude, longitude, tab-separated,
UTF-8. It is embedded as a Qt resource at `:/text/places.tsv`.

It exists because the USGS event service replaces such letters by "?" in the
place names of its older earthquakes ("13 km N of Do?anyol, Turkey"), in every
output format. `text::repairPlace` puts the letters back when exactly one
place near the earthquake fits the pattern.

| | |
|---|---|
| Source | GeoNames, `cities500.zip` (cities with a population over 500 or seats of administrative divisions), https://download.geonames.org/export/dump/ |
| Licence | Creative Commons Attribution 4.0, https://creativecommons.org/licenses/by/4.0/ ("This work is licensed under a Creative Commons Attribution 4.0 License", the dump's `readme.txt`) |
| Retrieved | 2026-11-10 |
| Changes | reduced to the box above and to names with a non-ASCII letter; three columns kept; coordinates rounded to three decimals; sorted |
| Made by | `packaging/geonames/make_places.py cities500.zip places.tsv`; the record of that run is `places.tsv.provenance.json` (input and output SHA-256, row counts) |

On the bundled earthquake list (`src/tdvms/data/events.csv`) the repair
restores 100 of the 101 damaged names; "As Sulaym?n?yah" stays as it is.
