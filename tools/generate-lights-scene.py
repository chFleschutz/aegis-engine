#!/usr/bin/env python3
"""Writes the light stress scene, scenes/stress/lights.scene, into the sample project.

1024 point lights on a 32 x 32 grid just above a floor, between a grid of small cubes, and no directional light: a
scene whose lighting cost scales with the light count, for measuring the lighting pass ("Lighting" in the profiler)
and checking that every light is shaded. The colors and IDs come from a fixed seed, so running it again writes the
same file.

Usage: tools/generate-lights-scene.py [output] [lights]
       (defaults: samples/content/scenes/stress/lights.scene and 1024 lights, e.g. 128 to compare)
"""

import colorsys
import json
import math
import random
import re
import sys
from pathlib import Path

LIGHTS = 1024  # Laid out on a square grid (32 x 32)
LIGHT_SPACING = 2.0
LIGHT_HEIGHT = 0.5
LIGHT_INTENSITY = 5.0
LIGHT_RANGE = 3.0
CUBE_SPACING = 4.0
CUBE_SCALE = 0.3
FLOOR_THICKNESS = 0.1
SEED = 255

# Sample project assets
CUBE_MESH = "1f57e55da564f514"
FLOOR_MATERIAL = "9ad18c73d472ee5f"  # cube_7, a pale grey-blue
CUBE_MATERIAL = "da8afe6c2c394909"  # cube_6, orange
SKY_ENVIRONMENT = "09e80a13cdf16414"

rng = random.Random(SEED)
used_ids = set()


def new_id():
    while True:
        value = rng.getrandbits(64)
        if value and value not in used_ids:
            used_ids.add(value)
            return f"{value:016x}"


def transform(location=(0, 0, 0), rotation=(0, 0, 0, 1), scale=(1, 1, 1)):
    return {"location": list(location), "rotation": list(rotation), "scale": list(scale)}


def entity(name, components, scripts=None):
    result = {"id": new_id(), "name": name, "children": [], "components": components}
    if scripts:
        result["scripts"] = scripts
    return result


def grid(count, spacing):
    """'count' positions on the smallest square grid that holds them, centered on the origin, row by row."""
    side = math.isqrt(count - 1) + 1 if count else 0
    offset = (side - 1) * spacing / 2
    return [(x * spacing - offset, y * spacing - offset) for y in range(side) for x in range(side)][:count]


def build(light_count):
    entities = []

    for i, (x, y) in enumerate(grid(light_count, LIGHT_SPACING)):
        color = colorsys.hsv_to_rgb(rng.random(), 0.75, 1.0)
        entities.append(entity(f"Light {i:04}", {
            "PointLight": {"color": [round(c, 4) for c in color], "intensity": LIGHT_INTENSITY, "range": LIGHT_RANGE},
            "Transform": transform((x, y, LIGHT_HEIGHT), scale=(0.25, 0.25, 0.25)),
        }))

    side = math.isqrt(light_count - 1) + 1 if light_count else 0
    cubes = round(side * LIGHT_SPACING / CUBE_SPACING) ** 2
    for i, (x, y) in enumerate(grid(cubes, CUBE_SPACING)):
        entities.append(entity(f"Cube {i:03}", {
            "MeshRenderer": {"mesh": CUBE_MESH, "material": CUBE_MATERIAL},
            "Transform": transform((x, y, CUBE_SCALE), scale=(CUBE_SCALE, CUBE_SCALE, CUBE_SCALE)),
        }))

    # The cube mesh spans -1 to 1: a flat cube one light spacing wider than the grid, its top at 0
    half_width = side * LIGHT_SPACING / 2
    entities.append(entity("Floor", {
        "MeshRenderer": {"mesh": CUBE_MESH, "material": FLOOR_MATERIAL},
        "Transform": transform((0, 0, -FLOOR_THICKNESS), scale=(half_width, half_width, FLOOR_THICKNESS)),
    }))
    # Looks at the grid from above one corner (the rotation of scenes/stress/hierarchy.scene's camera)
    entities.append(entity("Main Camera", {
        "Camera": {"fov": 1.0471976, "near": 0.1, "far": 1000},
        "DynamicTag": {},
        "MainCamera": {},
        "Transform": transform((-40, -40, 26.4), (-0.23911761, 0.09904577, -0.36964384, 0.8923991)),
    }, scripts=[{"KinematicMovementController": {"moveSpeed": 5, "lookSpeed": 1.5, "mouseSensitivity": 0.004, "mousePanSpeed": 0.02}}]))
    entities.append(entity("Ambient Light", {"AmbientLight": {"color": [1, 1, 1], "intensity": 0.05}, "Transform": transform()}))
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
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "samples" / "content" / "scenes" / "stress" / "lights.scene"
    light_count = int(sys.argv[2]) if len(sys.argv) > 2 else LIGHTS
    scene = build(light_count)
    output.write_text(dump(scene), encoding="utf-8", newline="\n")
    print(f"Wrote {len(scene['entities'])} entities to {output}")


if __name__ == "__main__":
    main()
