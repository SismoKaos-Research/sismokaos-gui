"""Clip and simplify Natural Earth's countries for the Regions maps.

Usage (the zip is data, not code; run isolated):
    uv run --no-project --with pyshp==2.3.1 --with shapely==2.0.6 python -I \
        clip_countries.py ne_10m_admin_0_countries_tur.zip countries.geojson

Reads the Admin 0 countries shapefile from the zip, keeps the part of every
country inside the box below, simplifies it and writes a compact GeoJSON
whose features carry `name` and `sovereignt`. See
src/analysis/data/PROVENANCE.md.
"""

import io
import json
import sys
import zipfile

import shapefile  # pyshp
from shapely.geometry import box, mapping, shape

BOX = (24.0, 32.0, 48.0, 44.0)  # lon min, lat min, lon max, lat max
TOLERANCE_DEG = 0.004  # Douglas-Peucker, about 400 m
DECIMALS = 3  # about 100 m


def rounded(coords):
    if isinstance(coords[0], (int, float)):
        return [round(coords[0], DECIMALS), round(coords[1], DECIMALS)]
    return [rounded(c) for c in coords]


def main(src, dst):
    with zipfile.ZipFile(src) as z:
        names = z.namelist()
        part = {ext: io.BytesIO(z.read(next(n for n in names if n.endswith(ext))))
                for ext in (".shp", ".dbf", ".shx")}
    reader = shapefile.Reader(shp=part[".shp"], dbf=part[".dbf"], shx=part[".shx"],
                              encoding="utf-8")
    clip = box(*BOX)
    features = []
    for sr in reader.iterShapeRecords():
        rec = sr.record.as_dict()
        geom = shape(sr.shape.__geo_interface__)
        if not geom.is_valid:
            geom = geom.buffer(0)
        geom = geom.intersection(clip)
        if geom.is_empty:
            continue
        geom = geom.simplify(TOLERANCE_DEG, preserve_topology=True)
        polys = [g for g in getattr(geom, "geoms", [geom])
                 if g.geom_type == "Polygon" and g.area > 1e-4]
        if not polys:
            continue
        coords = [rounded(mapping(p)["coordinates"]) for p in polys]
        features.append({
            "type": "Feature",
            "properties": {"name": rec["NAME"], "sovereignt": rec["SOVEREIGNT"]},
            "geometry": {"type": "MultiPolygon", "coordinates": coords},
        })
    features.sort(key=lambda f: f["properties"]["name"])
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"type": "FeatureCollection", "features": features}, f,
                  ensure_ascii=False, separators=(",", ":"))
        f.write("\n")
    if not features:
        sys.exit("nothing inside the box")
    print(len(features), "countries:", ", ".join(f["properties"]["name"] for f in features))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
