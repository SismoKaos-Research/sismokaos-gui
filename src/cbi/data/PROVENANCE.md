# Data embedded with the indicators library

- `afad_magnitude_map.json`: the rate-matching conversion of AFAD magnitudes
  before 2013-11-20 onto the 2014-2019 ML/Mw practice, fitted by the
  `calibrate-magnitudes` command of SismoKaos-Research/indicators-forecast
  (the tested fork of SismoKaos-Catalog-Based-Indicators) on the national
  AFAD catalog (Gardner-Knopoff declustered); copied unchanged from the
  fork's `src/sismokaos_indicators/` on 2026-11-10. The GUI can refit it
  from the Indicators tab.

The East Anatolian Fault Zone traces (`eafz.geojson`, GEM GAF-DB, CC BY-SA
4.0) are no longer embedded: they ship as a separate data file, see
`data/faults/PROVENANCE.md`.
