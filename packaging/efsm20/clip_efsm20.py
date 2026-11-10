"""Clip EFSM20's crustal fault top traces to Turkey and its neighbours.

Usage (the download is data, not code; run isolated, standard library only):
    uv run --no-project python -I clip_efsm20.py efsm20_cf_top.json faults_tr.geojson

The input is the layer `EFSM20:efsm20_cf_top` as GeoJSON in EPSG:4326, from
    https://services.seismofaults.eu/EFSM20/ows?service=WFS&version=2.0.0
        &request=GetFeature&typeNames=EFSM20:efsm20_cf_top
        &outputFormat=application/json&srsName=EPSG:4326
Keeps every trace that touches the box below, whole (a trace is never cut),
simplifies it lightly, rounds the coordinates and keeps five attributes.
Writes <output>.provenance.json beside the output and exits nonzero when
nothing is kept. See data/faults/PROVENANCE.md.
"""

import datetime
import hashlib
import json
import math
import sys

BOX = (24.0, 32.0, 48.0, 44.0)  # lon min, lat min, lon max, lat max (as the countries clip)
TOLERANCE_DEG = 0.002  # Douglas-Peucker, about 200 m
DECIMALS = 4  # about 10 m
SLIP = {"N": "Normal", "R": "Reverse", "RL": "Dextral", "LL": "Sinistral"}


def segment_hits_box(a, b, box):
    """Liang-Barsky: does the segment a-b touch the box?"""
    x0, y0, x1, y1 = box
    dx, dy = b[0] - a[0], b[1] - a[1]
    t0, t1 = 0.0, 1.0
    for p, q in ((-dx, a[0] - x0), (dx, x1 - a[0]), (-dy, a[1] - y0), (dy, y1 - a[1])):
        if p == 0:
            if q < 0:
                return False
            continue
        t = q / p
        if p < 0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
        if t0 > t1:
            return False
    return True


def touches(line, box):
    if len(line) == 1:
        return box[0] <= line[0][0] <= box[2] and box[1] <= line[0][1] <= box[3]
    return any(segment_hits_box(line[i], line[i + 1], box) for i in range(len(line) - 1))


def simplify(line, tol):
    """Douglas-Peucker in degrees, longitude scaled by cos(latitude)."""
    if len(line) < 3:
        return line
    k = math.cos(math.radians(sum(p[1] for p in line) / len(line)))
    keep = [False] * len(line)
    keep[0] = keep[-1] = True
    stack = [(0, len(line) - 1)]
    while stack:
        i, j = stack.pop()
        ax, ay = line[i][0] * k, line[i][1]
        bx, by = line[j][0] * k, line[j][1]
        dx, dy = bx - ax, by - ay
        den = dx * dx + dy * dy
        worst, at = 0.0, -1
        for m in range(i + 1, j):
            px, py = line[m][0] * k - ax, line[m][1] - ay
            t = max(0.0, min(1.0, (px * dx + py * dy) / den)) if den else 0.0
            d = math.hypot(px - t * dx, py - t * dy)
            if d > worst:
                worst, at = d, m
        if worst > tol:
            keep[at] = True
            stack += [(i, at), (at, j)]
    return [p for p, k_ in zip(line, keep) if k_]


def rounded(line):
    out = []
    for p in line:
        q = [round(p[0], DECIMALS), round(p[1], DECIMALS)]
        if not out or out[-1] != q:
            out.append(q)
    return out


def main(src, dst):
    raw = open(src, "rb").read()
    data = json.loads(raw)
    crs = data.get("crs", {}).get("properties", {}).get("name", "")
    if "4326" not in crs and "CRS84" not in crs:
        sys.exit("unexpected CRS: %r" % crs)
    features, before, after, outside = [], 0, 0, 0
    for f in data["features"]:
        g = f["geometry"]
        lines = [g["coordinates"]] if g["type"] == "LineString" else g["coordinates"]
        lines = [[(p[0], p[1]) for p in line] for line in lines]
        if not any(touches(line, BOX) for line in lines):
            continue
        p = f["properties"]
        kept = []
        for line in lines:
            before += len(line)
            s = rounded(simplify(line, TOLERANCE_DEG))
            if len(s) >= 2:
                kept.append(s)
                after += len(s)
                outside += sum(1 for q in s if not (BOX[0] <= q[0] <= BOX[2] and BOX[1] <= q[1] <= BOX[3]))
        if not kept:
            continue
        features.append({
            "type": "Feature",
            "properties": {
                "id": p["idfs"],
                "fault_type": p["faulttype"],
                "slip_type": SLIP.get(p["faulttype"], ""),
                "slip_rate_mm_yr": round(float(p["sramean"]), 2),
                "mw_max": round(float(p["mwmaxavg"]), 2),
            },
            "geometry": ({"type": "LineString", "coordinates": kept[0]} if len(kept) == 1
                         else {"type": "MultiLineString", "coordinates": kept}),
        })
    features.sort(key=lambda f: f["properties"]["id"])
    out = {
        "type": "FeatureCollection",
        "name": "Seismogenic fault sources, EFSM20",
        "metadata": {
            "description": "EFSM20 crustal fault sources, top traces, Turkey and its neighbours "
                           "(a hazard-model fault-source set, not an active fault map).",
            "source": "European Fault-Source Model 2020 (EFSM20), INGV, "
                      "https://doi.org/10.13127/efsm20",
            "license": "CC BY 4.0",
            "changes": "clipped to lon %g-%g, lat %g-%g (whole traces kept), simplified "
                       "(Douglas-Peucker %g deg), rounded to %d decimals, attributes reduced"
                       % (BOX[0], BOX[2], BOX[1], BOX[3], TOLERANCE_DEG, DECIMALS),
        },
        "features": features,
    }
    if not features:
        sys.exit("nothing inside the box")
    body = json.dumps(out, ensure_ascii=False, separators=(",", ":")) + "\n"
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    types = {}
    for f in features:
        types[f["properties"]["fault_type"]] = types.get(f["properties"]["fault_type"], 0) + 1
    record = {
        "argv": sys.argv,
        "python": sys.version.split()[0],
        "date_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "parameters": {"box": BOX, "tolerance_deg": TOLERANCE_DEG, "decimals": DECIMALS},
        "input": {"path": src, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                  "features": len(data["features"]), "wfs_timestamp": data.get("timeStamp")},
        "output": {"path": dst, "bytes": len(body.encode("utf-8")),
                   "sha256": hashlib.sha256(body.encode("utf-8")).hexdigest(),
                   "features": len(features), "by_fault_type": types,
                   "vertices_before": before, "vertices_after": after,
                   "vertices_outside_box": outside},
    }
    with open(dst + ".provenance.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(record, f, indent=1)
        f.write("\n")
    print(json.dumps(record["output"]))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
