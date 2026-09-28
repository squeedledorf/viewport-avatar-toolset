#!/usr/bin/env python3
"""Converts a glTF binary (.glb) model into a VATs starter prop (app/assets/props/*.dae, see CREDITS.md there).

COLLADA 1.4, Z up, metres, triangles only; flat colours from the shared muted palette instead of textures;
degenerate triangles dropped. The model is turned to SL axes, scaled to a real-world size and moved so the
origin sits at a chosen point (a handheld item's grip).

  glb-to-prop.py model.glb --list                      nodes, materials and the Z-up bounding box
  glb-to-prop.py model.glb out.dae [--rot z90] --size 0.2 --origin 0.2,0.5,0.35 [--colors 0=wood,1=steel]

--rot     rotations in degrees about the output axes, applied in order after glTF Y-up -> Z-up.
--size    the length of the longest bounding-box side after rotation, in metres.
--origin  the new origin on X, Y and Z: a fraction of the rotated, scaled bounding box (0 = min, 1 = max),
          or metres from its minimum with an "m" suffix (0.039m,0.5,0.044m).
--colors  material index -> palette name; unlisted materials take the palette colour nearest their own.

The weapons pack, from the GLB downloads of the poly.pizza pages in CREDITS.md:
  pistol.glb     pistol.dae     --size 0.2 --origin 0.039m,0.5,0.044m --colors 0=darkwood,1=gunmetal,2=dark,3=gunmetal,4=black
  rifle.glb      rifle.dae      --size 0.88 --origin 0.265m,0.5,0.128m --colors 0=wood,1=black,2=dark,3=gunmetal,4=darkwood
  shotgun.glb    shotgun.dae    --size 1.0 --origin 0.262m,0.5,0.088m --colors 0=wood,1=gunmetal,2=dark,3=black
  knife.glb      knife.dae      --rot z90 --size 0.3 --origin 0.5,0.5,0.04m --colors 0=gunmetal,1=black,2=steel
  claymore.glb   greatsword.dae --rot z90 --size 1.35 --origin 0.5,0.5,0.32m --colors 0=gunmetal,1=black,2=darkwood,3=ice,4=steel
"""
import argparse, json, math, re, struct, sys

import numpy as np

# The shared muted palette of the starter props.
PALETTE = {
    "steel": (0.620, 0.660, 0.710), "gunmetal": (0.360, 0.390, 0.430), "dark": (0.240, 0.250, 0.280),
    "black": (0.200, 0.230, 0.270), "wood": (0.620, 0.470, 0.340), "darkwood": (0.400, 0.300, 0.230),
    "brass": (0.800, 0.600, 0.330), "tan": (0.680, 0.580, 0.360), "cream": (0.800, 0.760, 0.680),
    "blue": (0.420, 0.490, 0.580), "ice": (0.700, 0.760, 0.800), "brick": (0.620, 0.380, 0.300),
    "wine": (0.460, 0.200, 0.250), "green": (0.470, 0.560, 0.420), "teal": (0.330, 0.520, 0.520),
}
CTYPE = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_glb(path):
    data = open(path, "rb").read()
    magic, _, _ = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67, "not a GLB"
    off, doc, blob = 12, None, b""
    while off < len(data):
        ln, kind = struct.unpack_from("<II", data, off)
        chunk = data[off + 8:off + 8 + ln]
        if kind == 0x4E4F534A:
            doc = json.loads(chunk)
        elif kind == 0x004E4942:
            blob = chunk
        off += 8 + ln
    return doc, blob


def accessor(doc, blob, i):
    a = doc["accessors"][i]
    bv = doc["bufferViews"][a["bufferView"]]
    n, ct = NCOMP[a["type"]], CTYPE[a["componentType"]]
    size = struct.calcsize("<" + ct)
    stride = bv.get("byteStride", size * n)
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = np.empty((a["count"], n), dtype=np.float64)
    for k in range(a["count"]):
        out[k] = struct.unpack_from("<" + ct * n, blob, base + k * stride)
    return out


def node_matrix(nd):
    if "matrix" in nd:
        return np.array(nd["matrix"], dtype=np.float64).reshape(4, 4).T
    t = np.eye(4)
    t[:3, 3] = nd.get("translation", [0, 0, 0])
    x, y, z, w = nd.get("rotation", [0, 0, 0, 1])
    r = np.eye(4)
    r[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                 [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                 [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    s = np.diag(nd.get("scale", [1, 1, 1]) + [1])
    return t @ r @ s


def material_colour(doc, mi):
    if mi is None:
        return (0.7, 0.7, 0.7), "(none)"
    m = doc["materials"][mi]
    c = m.get("pbrMetallicRoughness", {}).get("baseColorFactor", [0.7, 0.7, 0.7, 1])
    return tuple(c[:3]), m.get("name", f"material {mi}")


def triangles(doc, blob):
    """(positions Nx3, material per triangle) in glTF space, every mesh node of the default scene."""
    tris, mats = [], []

    def walk(ni, parent):
        nd = doc["nodes"][ni]
        m = parent @ node_matrix(nd)
        if "mesh" in nd:
            for prim in doc["meshes"][nd["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    continue
                p = accessor(doc, blob, prim["attributes"]["POSITION"])
                p = (np.c_[p, np.ones(len(p))] @ m.T)[:, :3]
                idx = accessor(doc, blob, prim["indices"]).astype(int).ravel() if "indices" in prim else np.arange(len(p))
                t = p[idx.reshape(-1, 3)]
                if np.linalg.det(m[:3, :3]) < 0:
                    t = t[:, ::-1]
                tris.append(t)
                mats.extend([prim.get("material")] * len(t))
        for c in nd.get("children", []):
            walk(c, m)

    scene = doc["scenes"][doc.get("scene", 0)]
    for r in scene["nodes"]:
        walk(r, np.eye(4))
    return np.concatenate(tris), mats


def rot_matrix(spec):
    r = np.eye(3)
    for part in filter(None, (spec or "").split(",")):
        ax, deg = part[0], math.radians(float(part[1:]))
        c, s = math.cos(deg), math.sin(deg)
        m = {"x": [[1, 0, 0], [0, c, -s], [0, s, c]], "y": [[c, 0, s], [0, 1, 0], [-s, 0, c]],
             "z": [[c, -s, 0], [s, c, 0], [0, 0, 1]]}[ax]
        r = np.array(m) @ r
    return r


def nearest(c):
    return min(PALETTE, key=lambda k: sum((a - b) ** 2 for a, b in zip(PALETTE[k], c)))


def g(v, places):
    """Fixed point without trailing zeros: 0.1 mm for positions, 0.001 for normals keeps the files small."""
    s = f"{v:.{places}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "", "-") else s


def write_dae(path, name, tris, mats, colours):
    used = sorted(set(mats), key=lambda m: (m is None, m if m is not None else 0))
    pal = {m: colours[m] for m in used}
    keys = sorted(set(pal.values()), key=list(PALETTE).index)
    verts, vindex, groups = [], {}, {k: [] for k in keys}
    for t, m in zip(tris, mats):
        n = np.cross(t[1] - t[0], t[2] - t[0])
        n /= np.linalg.norm(n)
        ids = []
        for p in t:
            key = (tuple(round(x, 4) + 0.0 for x in p), tuple(round(x, 3) + 0.0 for x in n))
            if key not in vindex:
                vindex[key] = len(verts)
                verts.append(key)
            ids.append(vindex[key])
        groups[pal[m]].append(ids)
    fx = "".join(f'<effect id="m{i}-fx"><profile_COMMON><technique sid="common"><lambert><diffuse><color>'
                 f'{PALETTE[k][0]:.3f} {PALETTE[k][1]:.3f} {PALETTE[k][2]:.3f} 1</color></diffuse></lambert>'
                 f'</technique></profile_COMMON></effect>\n' for i, k in enumerate(keys))
    mt = "".join(f'<material id="m{i}" name="m{i}"><instance_effect url="#m{i}-fx"/></material>\n' for i in range(len(keys)))
    pos = " ".join(g(x, 4) for v in verts for x in v[0])
    nrm = " ".join(g(x, 3) for v in verts for x in v[1])
    tri = "".join(f'<triangles material="m{i}" count="{len(groups[k])}"><input semantic="VERTEX" source="#g-v" offset="0"/>'
                  f'<p>{" ".join(str(x) for ids in groups[k] for x in ids)}</p></triangles>\n' for i, k in enumerate(keys))
    inst = "".join(f'<instance_material symbol="m{i}" target="#m{i}"/>\n' for i in range(len(keys)))
    with open(path, "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">\n'
                '<asset><contributor><authoring_tool>VATs starter props (see CREDITS.md)</authoring_tool></contributor>'
                '<unit name="meter" meter="1"/><up_axis>Z_UP</up_axis></asset>\n'
                f'<library_effects>\n{fx}</library_effects><library_materials>\n{mt}</library_materials>'
                f'<library_geometries><geometry id="g" name="{name}"><mesh>\n'
                f'<source id="g-p"><float_array id="g-p-a" count="{len(verts) * 3}">{pos}</float_array><technique_common>'
                f'<accessor source="#g-p-a" count="{len(verts)}" stride="3"><param name="X" type="float"/>'
                '<param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>\n'
                f'<source id="g-n"><float_array id="g-n-a" count="{len(verts) * 3}">{nrm}</float_array><technique_common>'
                f'<accessor source="#g-n-a" count="{len(verts)}" stride="3"><param name="X" type="float"/>'
                '<param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>\n'
                '<vertices id="g-v"><input semantic="POSITION" source="#g-p"/><input semantic="NORMAL" source="#g-n"/></vertices>\n'
                f'{tri}</mesh></geometry></library_geometries><library_visual_scenes><visual_scene id="Scene">'
                f'<node id="n" name="{name}"><instance_geometry url="#g"><bind_material><technique_common>\n{inst}'
                '</technique_common></bind_material></instance_geometry></node></visual_scene></library_visual_scenes>'
                '<scene><instance_visual_scene url="#Scene"/></scene></COLLADA>\n')
    return len(verts), sum(len(v) for v in groups.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("glb")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--rot", default="")
    ap.add_argument("--size", type=float)
    ap.add_argument("--origin", default="0.5,0.5,0.5")
    ap.add_argument("--colors", default="")
    ap.add_argument("--name")
    a = ap.parse_args()

    doc, blob = load_glb(a.glb)
    tris, mats = triangles(doc, blob)
    tris = tris[..., [0, 2, 1]] * [1, -1, 1]  # glTF Y up -> Z up: a quarter turn about X, then --rot
    tris = tris @ rot_matrix(a.rot).T
    area = np.linalg.norm(np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1)
    keep = area > 1e-12 * max(1.0, float(np.ptp(tris)) ** 2)
    dropped = int((~keep).sum())
    tris, mats = tris[keep], [m for m, k in zip(mats, keep) if k]
    lo, hi = tris.reshape(-1, 3).min(0), tris.reshape(-1, 3).max(0)
    if a.list:
        for i, nd in enumerate(doc["nodes"]):
            print(f"node {i}: {nd.get('name')!r} mesh={nd.get('mesh')} children={nd.get('children', [])}")
        for i in sorted(set(mats), key=str):
            c, n = material_colour(doc, i)
            print(f"material {i}: {n!r} {tuple(round(x, 3) for x in c)} -> {nearest(c)} ({mats.count(i)} tris)")
        print("bbox", lo.round(4), hi.round(4), "size", (hi - lo).round(4), "tris", len(tris), "degenerate", dropped)
        return
    scale = a.size / float((hi - lo).max()) if a.size else 1.0
    tris, lo, hi = tris * scale, lo * scale, hi * scale
    o = np.array([lo[i] + (float(v[:-1]) if v.endswith("m") else (hi[i] - lo[i]) * float(v))
                  for i, v in enumerate(a.origin.split(","))])
    tris, lo, hi = tris - o, lo - o, hi - o
    colours = {m: nearest(material_colour(doc, m)[0]) for m in set(mats)}
    for part in filter(None, a.colors.split(",")):
        k, v = part.split("=")
        colours[None if k == "none" else int(k)] = v
    name = a.name or re.sub(r"\.dae$", "", a.out.split("/")[-1])
    nv, nt = write_dae(a.out, name, tris, mats, colours)
    c = (lo + hi) / 2
    print(f"{a.out}: {nt} tris, {nv} verts, {dropped} degenerate dropped; bbox {lo.round(4)} .. {hi.round(4)}, "
          f"size {(hi - lo).round(4)}, centre {c.round(4)}")


if __name__ == "__main__":
    main()
