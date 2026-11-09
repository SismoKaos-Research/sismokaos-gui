# Data embedded with the indicators library

Both files come from SismoKaos-Research/indicators-forecast
(`src/sismokaos_indicators/`), the tested fork of
SismoKaos-Catalog-Based-Indicators, copied unchanged on 2026-11-10.

- `eafz.geojson`: the East Anatolian Fault Zone as fault sections of the
  GEM Global Active Faults Database (Styron & Pagani 2020), EMME sections
  (Danciu et al. 2018), https://github.com/GEMScienceTools/gem-global-active-faults.
  Licence CC BY-SA 4.0. Two strands: `main` (Karlıova to Antakya) and
  `northern` (Sürgü-Çardak).
- `afad_magnitude_map.json`: the rate-matching conversion of AFAD magnitudes
  before 2013-11-20 onto the 2014-2019 ML/Mw practice, fitted by the fork's
  `calibrate-magnitudes` command on the national AFAD catalog
  (Gardner-Knopoff declustered). The GUI can refit it from the Indicators tab.
