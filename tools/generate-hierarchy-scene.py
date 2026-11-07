#!/usr/bin/env python3
"""Writes the editor's large edit scene, scenes/stress/hierarchy.scene, into the sample project.

About 10,000 cubes in groups of 100 under 100 group entities, plus a few chains nested 20 deep: a scene whose edit world
(not only its play world) is large, for measuring the editor's per-frame UI cost ("Editor UI" in the profiler) and the
hierarchy at scale. The IDs come from a fixed seed, so running it again writes the same file.

Usage: tools/generate-hierarchy-scene.py [output]   (default: samples/content/scenes/stress/hierarchy.scene)
"""

import json
import random
import re
import sys
from pathlib import Path

GROUPS = 100  # Laid out on a square grid
CUBES_PER_GROUP = 100  # Laid out on a square grid inside their group
CHAINS = 5
CHAIN_DEPTH = 20
GROUP_SPACING = 40.0
CUBE_SPACING = 3.0
SEED = 310

# Sample project assets, as scenes/stress/cubes.scene uses them
CUBE_MESH = "1f57e55da564f514"
MATERIALS = [
    "00ec32f47e710a78", "0d47203ca48af3de", "0fe7fe59044875c6", "582b34fc4b536cd3", "75beeae89401f524",
    "9ad18c73d472ee5f", "aa59a60926ef9ae9", "c595eb3a47d12160", "da8afe6c2c394909", "ef9b76c1df6fd914",
]
SKY_ENVIRONMENT = "09e80a13cdf16414"

rng = random.Random(SEED)
used_ids = set()


def new_id():
    while True:
        value = rng.getrandbits(64)
        if value and value not in used_ids:
            used_ids.add(value)
            return f"{value:016x}"


def transform(location=(0, 0, 0), rotation=(0, 0, 0, 1)):
    return {"location": list(location), "rotation": list(rotation), "scale": [1, 1, 1]}


def entity(name, components, children=(), scripts=None):
    result = {"id": new_id(), "name": name, "children": [child["id"] for child in children], "components": components}
    if scripts:
        result["scripts"] = scripts
    return result


def cube(name, location, material):
    return entity(name, {"MeshRenderer": {"mesh": CUBE_MESH, "material": material}, "Transform": transform(location)})


def grid(count, spacing):
    """'count' positions on a square grid centered on the origin."""
    side = round(count ** 0.5)
    offset = (side - 1) * spacing / 2
    return [(x * spacing - offset, y * spacing - offset) for y in range(side) for x in range(side)][:count]


def build():
    entities = []

    for g, (gx, gy) in enumerate(grid(GROUPS, GROUP_SPACING)):
        cubes = [cube(f"Cube {g:02}.{c:02}", (cx, cy, 0), MATERIALS[(g + c) % len(MATERIALS)])
                 for c, (cx, cy) in enumerate(grid(CUBES_PER_GROUP, CUBE_SPACING))]
        entities += cubes
        entities.append(entity(f"Group {g:02}", {"Transform": transform((gx, gy, 0))}, cubes))

    # Each chain is a tower of cubes, each link the child of the one below it
    edge = (round(GROUPS ** 0.5) / 2 + 1) * GROUP_SPACING
    for chain in range(CHAINS):
        child = None
        for depth in range(CHAIN_DEPTH, 0, -1):
            link = cube(f"Link {chain + 1}.{depth:02}", (0, 0, CUBE_SPACING if depth > 1 else 0), MATERIALS[depth % len(MATERIALS)])
            link["children"] = [child["id"]] if child else []
            entities.append(link)
            child = link
        root = entity(f"Chain {chain + 1}", {"Transform": transform((edge, (chain - CHAINS / 2) * GROUP_SPACING, 0))}, [child])
        entities.append(root)

    entities.append(entity("Main Camera", {
        "Camera": {"fov": 1.0471976, "near": 0.1, "far": 2000},
        "DynamicTag": {},
        "MainCamera": {},
        "Transform": transform((-500, -500, 330), (-0.23911761, 0.09904577, -0.36964384, 0.8923991)),
    }, scripts=[{"KinematicMovementController": {"moveSpeed": 5, "lookSpeed": 1.5, "mouseSensitivity": 0.004, "mousePanSpeed": 0.02}}]))
    entities.append(entity("Directional Light", {
        "DirectionalLight": {"color": [1, 1, 1], "intensity": 2},
        "Transform": transform(rotation=(0.46193975, 0.19134173, 0.3314136, 0.8001031)),
    }))
    entities.append(entity("Ambient Light", {"AmbientLight": {"color": [1, 1, 1], "intensity": 0.25}, "Transform": transform()}))
    entities.append(entity("Skybox", {"Sky": {"environment": SKY_ENVIRONMENT}, "Transform": transform()}))

    types = sorted({name for e in entities for name in e["components"]} | {name for e in entities for s in e.get("scripts", []) for name in s})
    return {
        "type": "Scene",
        "id": new_id(),
        "version": 1,
        "types": {name: 1 for name in types},
        "entities": sorted(entities, key=lambda e: e["id"]),  # As the editor saves them
    }


def dump(scene):
    """Two-space JSON with arrays of plain values on one line, as the editor writes scene files."""
    text = json.dumps(scene, indent=2)
    return re.sub(r"\[\s+([^\[\]{}]*?)\s+\]", lambda m: "[" + re.sub(r",\s+", ", ", m.group(1)) + "]", text) + "\n"


def main():
    root = Path(__file__).resolve().parent.parent
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "samples" / "content" / "scenes" / "stress" / "hierarchy.scene"
    scene = build()
    output.write_text(dump(scene), encoding="utf-8", newline="\n")
    print(f"Wrote {len(scene['entities'])} entities to {output}")


if __name__ == "__main__":
    main()
