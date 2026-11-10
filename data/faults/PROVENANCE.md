# Fault data shipped beside the program

The two GeoJSON files in this folder are data files with their own licences.
They are distributed alongside the program (installed as files next to it,
see `DEVELOPING.md`), not compiled into it, and are not covered by the
program's GPL-3.0-or-later licence. `LICENSE.txt` in this folder names the
licence of each.

| File | What it is | Licence |
|---|---|---|
| `faults_tr.geojson` | Seismogenic fault sources of Turkey and its neighbours (EFSM20), the fault layer drawn on the maps | CC BY 4.0 |
| `eafz.geojson` | East Anatolian Fault Zone strands (GEM GAF-DB), the definition of the "EAFZ" catalog zone | CC BY-SA 4.0 |

# faults_tr.geojson: seismogenic fault sources of Turkey and its neighbours

- **What it is:** the top traces of the crustal fault sources of the European
  Fault-Source Model 2020 (EFSM20) that touch longitude 24–48°E, latitude
  32–44°N (the box of `countries.geojson`). It is a fault-source set built
  for a seismic hazard model: simplified, unnamed traces. It is not an
  active fault map and does not replace the MTA map.
- **Source:** WFS `https://services.seismofaults.eu/EFSM20/ows`, layer
  `EFSM20:efsm20_cf_top`, GeoJSON in EPSG:4326, 1,248 features, response
  timestamp 2026-11-10T17:20:56Z, 1,435,143 bytes, sha256
  `c4923f226f15f556b6121309943cec29a5a4ceb09776b6ef909b48e5b23d7fe2`.
  Landing page: https://www.seismofaults.eu/efsm20 (the DOI's target).
- **Licence:** CC BY 4.0, https://creativecommons.org/licenses/by/4.0/.
  - seismofaults.eu/efsm20, read 2026-11-10: "Except where otherwise noted,
    this dataset is licensed under a Creative Commons Attribution 4.0
    International (CC BY 4.0) license."
  - DataCite record of the DOI, `rightsList`: "Creative Commons Attribution
    4.0 International", SPDX `cc-by-4.0`.
- **Changes** (`packaging/efsm20/clip_efsm20.py`, Python standard library):
  - kept the 422 traces that touch the box, each one whole (never cut); 138
    of the kept vertices lie outside the box, the farthest at 53.4°E;
  - simplified (Douglas-Peucker, 0.002°, about 200 m): 6,714 vertices to 3,267;
  - coordinates rounded to 0.0001°;
  - attributes reduced to `id` (EFSM20 `idfs`), `fault_type` (`faulttype`:
    N, R, RL, LL), `slip_type` (Normal, Reverse, Dextral, Sinistral),
    `slip_rate_mm_yr` (`sramean`, 2 decimals), `mw_max` (`mwmaxavg`).
  - Result: 422 features (N 164, RL 108, R 87, LL 63), 131,950 bytes, sha256
    `a1a3ed8d0fc7076c78fba09f0b5c33624ff1cbd806844e61683ff88f0f2cfdef`.
    The run's record is `faults_tr.geojson.provenance.json`.

## Citation (bibliographic data from DataCite, retrieved 2026-11-10)

Basili R., Danciu L., Beauval C., Sesetyan K., Vilanova S. P., Adamia S.,
Arroucau P., Atanackov J., Baize S., Canora C., Caputo R., Carafa M. M. C.,
Cushing E. M., Custódio S., Demircioglu Tumsa M. B., Duarte J. C., Ganas A.,
García-Mayordomo J., Gómez de la Peña L., Gràcia E., Jamšek Rupnik P.,
Jomard H., Kastelic V., Maesano F. E., Martín-Banda R., Martínez-Loriente S.,
Neres M., Perea H., Sket-Motnikar B., Tiberti M. M., Tsereteli N.,
Tsironi V., Vallone R., Vanneste K., Zupančič P. (2022). European
Fault-Source Model 2020 (EFSM20): online data on fault geometry and activity
parameters. Istituto Nazionale di Geofisica e Vulcanologia (INGV).
https://doi.org/10.13127/efsm20

DataCite fields used (`https://api.datacite.org/dois/10.13127/efsm20`):
`doi` 10.13127/efsm20; `publicationYear` 2022; `dates` Issued 2022-10-25;
`publisher` "Istituto Nazionale di Geofisica e Vulcanologia (INGV)";
`titles[0].title` as above; `types.resourceType` "Database of Seismogenic
Faults"; `creators` 35 names in the order above; `url`
https://www.seismofaults.eu/efsm20; `version` empty.

Companion paper (Crossref, `https://api.crossref.org/works/10.5194/nhess-24-3945-2024`):
Basili R., Danciu L., Beauval C., et al. (36 authors, the 35 above and
Giardini D.) (2024). The European Fault-Source Model 2020 (EFSM20): geologic
input data for the European Seismic Hazard Model 2020. Natural Hazards and
Earth System Sciences, 24(11), 3945–3976.
https://doi.org/10.5194/nhess-24-3945-2024 (CC BY 4.0 per Crossref).
Crossref spells two names differently from DataCite: "Šket Motnikar" and
"Demircioglu Tumsa, Mine Betul".

## Attribution line for the app and the README

Fault traces: European Fault-Source Model 2020 (EFSM20), crustal fault
sources. Basili et al. (2022), INGV, https://doi.org/10.13127/efsm20,
CC BY 4.0. Clipped, simplified and reduced in attributes.

# eafz.geojson: the East Anatolian Fault Zone strands

- **What it is:** the East Anatolian Fault Zone as fault sections of the GEM
  Global Active Faults Database (Styron & Pagani 2020), EMME sections
  (Danciu et al. 2018). Two strands: `main` (Karlıova to Antakya) and
  `northern` (Sürgü-Çardak). The program uses it to define the "EAFZ" zone:
  catalog events within a buffer of these traces.
- **Source:** https://github.com/GEMScienceTools/gem-global-active-faults,
  taken through SismoKaos-Research/indicators-forecast
  (`src/sismokaos_indicators/`), the tested fork of
  SismoKaos-Catalog-Based-Indicators, and copied unchanged from there on
  2026-11-10. sha256
  `94f6262d0b5cbf2df8a57d425dd3320d8a460bc56c2655d1f69cd979ee25d9d3`.
- **Licence:** CC BY-SA 4.0,
  https://creativecommons.org/licenses/by-sa/4.0/. The GEM repository's
  README: "The dataset is freely and publicly available here, under a
  Creative Commons attribution sharealike license."
- **Changes** (made in the fork, not here): the sections of the two strands
  selected from the database and tagged with a `strand` property.
- **Why it is a separate file:** CC BY-SA 4.0 material may be relicensed
  under GPL version 3 but not under "version 3 or any later version", which
  is the program's licence. The file therefore stays under CC BY-SA 4.0 and
  travels beside the program as an aggregate. This is a reading of the
  licence texts, not legal advice.
- **Citation** (bibliographic data as given by the GEM repository; not yet
  checked against Crossref): Styron R., Pagani M. (2020). The GEM Global
  Active Faults Database. Earthquake Spectra, 36(1_suppl), 160–180.
  https://doi.org/10.1177/8755293020944182
