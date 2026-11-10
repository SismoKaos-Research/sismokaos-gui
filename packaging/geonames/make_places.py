#!/usr/bin/env python3
"""Places whose names USGS cannot spell.

The USGS event service replaces every letter outside Latin-1 by "?" in the
place names of older earthquakes ("13 km N of Do?anyol, Turkey"). This writes
the list of populated places, in the box the application covers, whose names
hold a letter outside ASCII (it also drops some Latin-1 ones), so the application can put the letters back (it matches
the "?" pattern against the names near the earthquake).

usage: make_places.py cities500.zip places.tsv [W E S N]

Input: cities500.zip of GeoNames (https://download.geonames.org/export/dump/,
CC BY 4.0). Output: name<TAB>lat<TAB>lon, UTF-8, sorted, plus
places.tsv.provenance.json. Standard library only. Exits nonzero when
nothing is written.
"""
import datetime
import hashlib
import io
import json
import os
import sys
import zipfile


def lossy(name: str) -> bool:
    try:
        name.encode("ascii")
        return False
    except UnicodeEncodeError:
        return True


def main(argv):
    if len(argv) not in (3, 7):
        print(__doc__, file=sys.stderr)
        return 2
    src, out = argv[1], argv[2]
    w, e, s, n = (
        (float(x) for x in argv[3:7]) if len(argv) == 7 else (24.0, 48.0, 32.0, 44.0)
    )
    raw = open(src, "rb").read()
    rows = set()
    seen = 0
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        member = [m for m in z.namelist() if m.endswith(".txt")][0]
        for line in z.read(member).decode("utf-8").splitlines():
            f = line.split("\t")
            if len(f) < 6:
                continue
            seen += 1
            name, lat, lon = f[1], float(f[4]), float(f[5])
            if not (w <= lon <= e and s <= lat <= n):
                continue
            if not lossy(name):
                continue
            rows.add((name, round(lat, 3), round(lon, 3)))
    if not rows:
        print("no place kept", file=sys.stderr)
        return 1
    body = "".join(
        f"{name}\t{lat:.3f}\t{lon:.3f}\n" for name, lat, lon in sorted(rows)
    )
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)
    prov = {
        "what": "populated places whose names hold a letter outside ASCII",
        "source": "GeoNames cities500.zip, https://download.geonames.org/export/dump/",
        "licence": "CC BY 4.0, https://creativecommons.org/licenses/by/4.0/",
        "argv": [os.path.basename(a) if os.sep in a else a for a in argv],
        "box_w_e_s_n": [w, e, s, n],
        "input_sha256": hashlib.sha256(raw).hexdigest(),
        "input_rows": seen,
        "output_rows": len(rows),
        "output_sha256": hashlib.sha256(body.encode("utf-8")).hexdigest(),
        "made_utc": datetime.datetime.now(datetime.timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        ),
        "python": sys.version.split()[0],
    }
    with open(out + ".provenance.json", "w", encoding="utf-8") as fh:
        json.dump(prov, fh, indent=2)
        fh.write("\n")
    print(f"{len(rows)} places of {seen} written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
