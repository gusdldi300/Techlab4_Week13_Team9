"""Create an editable rural-station DoF diorama using the existing Kurisu assets.

Run with Python 3 and Pillow. Authoring units are engine metres, +X forward,
+Y right, +Z up. OBJ export performs the importer's inverse axis conversion.
Only SunlitStation assets and DOF_SunlitStation.scene are written.
"""

from __future__ import annotations

import copy
import json
import math
import random
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "DefaultProject/Content"
DESTINATION = CONTENT / "Meshes/SunlitStation"
SCENES = CONTENT / "Scenes"
RANDOM = random.Random(241113)
ATLAS_SIZE = 2048
CELL_SIZE = 256
ATLAS = Image.new("RGB", (ATLAS_SIZE, ATLAS_SIZE))
TILES = {}
MESHES = []
ACTORS = []
HERO_POSITION = (8.5, -1.65, .40)
TRACK_CENTER = 1.0
TRAIN_FRONT = 26.0

COLORS = {
    "grass": "#819544", "grass_light": "#a9b557", "grass_dark": "#596b31",
    "leaf": "#6d8538", "leaf_light": "#a6aa4d", "leaf_dark": "#344e29",
    "leaf_gold": "#c0b766", "bark": "#65583c", "soil": "#887657",
    "gravel": "#8c8676", "stone_light": "#b3a996", "stone_dark": "#625e50",
    "wood": "#8e8064", "wood_light": "#a99c7b", "wood_dark": "#5f5948",
    "sleeper": "#5b4d38", "rust": "#76523b", "rail": "#bab8a8",
    "steel": "#444c49", "cream": "#ede5bf", "sign": "#e9e3c8",
    "train_blue": "#496b78", "train_dark": "#304e5d", "roof": "#777d74",
    "black": "#26352f", "gold": "#e2b24e", "flower_red": "#e97949",
    "flower_coral": "#f4996d", "flower_white": "#f7e9bc",
    "flower_purple": "#a292b5", "pollen": "#e9c34c", "glint": "#fff1b1",
    "sky": "#c6d4b3", "mountain": "#73846a",
}


def add(first, second):
    return tuple(a + b for a, b in zip(first, second))


def subtract(first, second):
    return tuple(a - b for a, b in zip(first, second))


def multiply(vector, scale):
    return tuple(value * scale for value in vector)


def cross(first, second):
    return (first[1] * second[2] - first[2] * second[1],
            first[2] * second[0] - first[0] * second[2],
            first[0] * second[1] - first[1] * second[0])


def unit(vector):
    length = math.sqrt(sum(value * value for value in vector))
    if length < 1e-12:
        raise ValueError("Degenerate geometry")
    return multiply(vector, 1.0 / length)


def transform(location=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)):
    return {"Location": dict(zip("XYZ", location)),
            "Rotation": dict(zip(("Pitch", "Yaw", "Roll"), rotation)),
            "Scale": dict(zip("XYZ", scale))}


def component(class_name, name, parent="", properties=None):
    return {"Class": class_name, "Name": name, "DefaultSubobjectName": name,
            "Parent": parent, "Properties": properties or {}}


def actor(class_name, name, root, components):
    return {"Class": class_name, "Name": name, "ParentActor": "",
            "ParentComponent": "", "RootComponentName": root,
            "Properties": {"ScriptPath": "", "bActorEnableCollision": False},
            "Components": components}


def font(size, japanese=True):
    candidates = (["YuGothB.ttc", "msgothic.ttc"] if japanese else ["arialbd.ttf"])
    for name in candidates:
        path = Path("C:/Windows/Fonts") / name
        if path.exists():
            return ImageFont.truetype(str(path), size)
    raise FileNotFoundError("A Windows Japanese font is required to regenerate signs")


def tile(name, image):
    index = len(TILES)
    if index >= 64:
        raise ValueError("Station atlas is full")
    left, top = index % 8 * CELL_SIZE, index // 8 * CELL_SIZE
    ATLAS.paste(image, (left, top))
    TILES[name] = (left + 8, top + 8, left + 248, top + 248)


def build_atlas():
    for name, color in COLORS.items():
        image = Image.new("RGB", (256, 256), color)
        draw = ImageDraw.Draw(image)
        base = tuple(bytes.fromhex(color[1:]))
        textured = name in {"grass", "gravel", "soil", "wood", "wood_light",
                            "wood_dark", "sleeper", "bark", "rust"}
        if textured:
            for _ in range(8000):
                x, y = RANDOM.randrange(256), RANDOM.randrange(256)
                delta = RANDOM.randint(-22, 22)
                rgb = tuple(max(0, min(255, value + delta)) for value in base)
                draw.point((x, y), fill=rgb)
            if name.startswith("wood") or name == "sleeper":
                for _ in range(100):
                    x, y = RANDOM.randrange(256), RANDOM.randrange(256)
                    delta = RANDOM.randint(-30, 12)
                    rgb = tuple(max(0, min(255, value + delta)) for value in base)
                    draw.line((x, y, min(255, x + RANDOM.randint(12, 100)), y),
                              fill=rgb, width=RANDOM.choice([1, 1, 2]))
            if name == "gravel":
                for _ in range(440):
                    x, y = RANDOM.randrange(256), RANDOM.randrange(256)
                    radius = RANDOM.randint(2, 8)
                    gray = RANDOM.randint(80, 175)
                    draw.ellipse((x, y, x + radius, y + radius * .65),
                                 fill=(gray, int(gray * .95), int(gray * .82)))
        tile(name, image)

    image = Image.new("RGB", (256, 256), COLORS["sign"])
    draw = ImageDraw.Draw(image)
    draw.rectangle((6, 6, 249, 249), outline="#b3ae91", width=4)
    draw.text((128, 69), "こもれび", font=font(40), fill="#3b574d", anchor="mm")
    draw.text((128, 109), "木漏れ日", font=font(29), fill="#536154", anchor="mm")
    draw.rectangle((8, 145, 248, 171), fill="#5f8270")
    draw.text((128, 157), "KOMOREBI", font=font(21, False), fill="#f5edce", anchor="mm")
    draw.text((128, 208), "森の駅  /  01", font=font(21), fill="#5c6757", anchor="mm")
    tile("station_name", image)

    for name, title, subtitle, background in [
        ("train_destination", "森の町", "LOCAL  /  01", "#243c37"),
        ("train_number", "01", "KOMOREBI", "#d6af50"),
        ("timetable", "時刻表", "08:10   10:25   12:40", "#e7e1c7"),
    ]:
        image = Image.new("RGB", (256, 256), background)
        draw = ImageDraw.Draw(image)
        foreground = "#f3e8bc" if name == "train_destination" else "#394d43"
        draw.text((128, 99), title, font=font(60), fill=foreground, anchor="mm")
        draw.text((128, 184), subtitle, font=font(15, False), fill=foreground, anchor="mm")
        if name == "timetable":
            for y in range(134, 240, 19):
                draw.line((22, y, 234, y), fill="#b8b69c", width=2)
        tile(name, image)

    image = Image.new("RGB", (256, 256), "#678280")
    draw = ImageDraw.Draw(image)
    for y in range(256):
        amount = y / 255
        draw.line((0, y, 255, y), fill=(int(166 - 110 * amount),
                                      int(190 - 112 * amount), int(170 - 100 * amount)))
    draw.polygon([(0, 12), (70, 0), (255, 195), (255, 230)], fill="#bcccb4")
    draw.rectangle((10, 177, 246, 255), fill="#344e4b")
    tile("glass", image)
    ATLAS.save(DESTINATION / "StationAtlas.png", optimize=True)


class Mesh:
    """One editable prop or vegetation patch, grouped into two material sections."""

    def __init__(self, name):
        self.name = name
        self.faces = defaultdict(list)
        MESHES.append(self)

    def face(self, points, texture, highlight=False, two_sided=False):
        normal = unit(cross(subtract(points[1], points[0]),
                            subtract(points[2], points[0])))
        material = "Highlight" if highlight else "Surface"
        self.faces[material].append((points, normal, texture))
        if two_sided:
            self.faces[material].append((list(reversed(points)), multiply(normal, -1), texture))

    def quad(self, center, right, up, texture, highlight=False, two_sided=False):
        self.face([subtract(subtract(center, multiply(right, .5)), multiply(up, .5)),
                   add(subtract(center, multiply(up, .5)), multiply(right, .5)),
                   add(add(center, multiply(right, .5)), multiply(up, .5)),
                   add(subtract(center, multiply(right, .5)), multiply(up, .5))],
                  texture, highlight, two_sided)

    def box(self, center, size, texture, *, omit_faces=()):
        x, y, z = center
        sx, sy, sz = (value / 2 for value in size)
        if "front" not in omit_faces:
            self.quad((x - sx, y, z), (0, -2 * sy, 0), (0, 0, 2 * sz), texture)
        if "back" not in omit_faces:
            self.quad((x + sx, y, z), (0, 2 * sy, 0), (0, 0, 2 * sz), texture)
        if "sides" not in omit_faces:
            self.quad((x, y - sy, z), (2 * sx, 0, 0), (0, 0, 2 * sz), texture)
            self.quad((x, y + sy, z), (-2 * sx, 0, 0), (0, 0, 2 * sz), texture)
        if "top" not in omit_faces:
            self.quad((x, y, z + sz), (2 * sx, 0, 0), (0, 2 * sy, 0), texture)
        if "bottom" not in omit_faces:
            self.quad((x, y, z - sz), (2 * sx, 0, 0), (0, -2 * sy, 0), texture)

    def tube(self, start, end, radius, texture, segments=8):
        direction = unit(subtract(end, start))
        reference = (0, 0, 1) if abs(direction[2]) < .95 else (0, 1, 0)
        right = multiply(unit(cross(direction, reference)), radius)
        up = cross(direction, right)
        rows = [[add(center, add(multiply(right, math.cos(i * math.tau / segments)),
                                 multiply(up, math.sin(i * math.tau / segments))))
                 for i in range(segments)] for center in (start, end)]
        for i in range(segments):
            next_index = (i + 1) % segments
            self.face([rows[0][i], rows[0][next_index], rows[1][next_index], rows[1][i]], texture)
        self.face(list(reversed(rows[0])), texture)
        self.face(rows[1], texture)

    def ellipsoid(self, center, radii, texture, segments=10, rings=6, highlight=False):
        rows = []
        for ring in range(rings + 1):
            angle = .002 + (math.pi - .004) * ring / rings
            rows.append([add(center, (radii[0] * math.sin(angle) * math.cos(i * math.tau / segments),
                                      radii[1] * math.sin(angle) * math.sin(i * math.tau / segments),
                                      radii[2] * math.cos(angle))) for i in range(segments)])
        for ring in range(rings):
            for i in range(segments):
                next_index = (i + 1) % segments
                self.face([rows[ring][i], rows[ring + 1][i],
                           rows[ring + 1][next_index], rows[ring][next_index]],
                          texture, highlight)

    def leaf(self, center, length, width, angle, texture):
        along = (math.cos(angle) * length * .5, math.sin(angle) * length * .5, length * .16)
        across = (-math.sin(angle) * width * .5, math.cos(angle) * width * .5, 0)
        ridge = add(center, (0, 0, width * .13))
        tip, base = add(center, along), subtract(center, along)
        left, right = subtract(center, across), add(center, across)
        for triangle in ([base, right, ridge], [right, tip, ridge],
                         [tip, left, ridge], [left, base, ridge]):
            self.face(triangle, texture, two_sided=True)

    def save(self, *, add_scene_actor=True):
        lines = ["# Generated by Setup/Scenes/GenerateSunlitStation.py",
                 "# OBJ Y-up; importer maps (-Z, X, Y) into engine metres.",
                 "mtllib Station.mtl", f"o {self.name}"]
        index = 1
        for material, faces in self.faces.items():
            lines.append(f"usemtl {material}")
            for points, normal, texture in faces:
                left, top, right, bottom = TILES[texture]
                uv = [(right, bottom), (left, bottom), (left, top), (right, top)]
                if len(points) != 4:
                    uv = [((left + right) / 2, (top + bottom) / 2)] * len(points)
                for point, texcoord in zip(points, uv):
                    lines.append(f"v {point[1]:.6f} {point[2]:.6f} {-point[0]:.6f}")
                    lines.append(f"vt {texcoord[0] / ATLAS_SIZE:.8f} {1 - texcoord[1] / ATLAS_SIZE:.8f}")
                    lines.append(f"vn {normal[1]:.6f} {normal[2]:.6f} {-normal[0]:.6f}")
                indices = range(index, index + len(points))
                lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in reversed(list(indices))))
                index += len(points)
        (DESTINATION / f"{self.name}.obj").write_text("\n".join(lines) + "\n", encoding="ascii")
        if not add_scene_actor:
            return
        mesh_component = component("UStaticMeshComponent", "UStaticMeshComponent", "DefaultSceneRoot", {
            "Transform": transform(), "StaticMesh": f"Meshes/SunlitStation/{self.name}.obj",
            "bVisible": True, "bHiddenInGame": False, "bCastShadow": True,
            "bReceiveShadow": True, "bBlockComponent": False, "bGenerateOverlapEvents": False})
        mesh_component["OverrideMaterials"] = []
        ACTORS.append(actor("AStaticMeshActor", f"SS_{self.name}", "DefaultSceneRoot", [
            component("USceneComponent", "DefaultSceneRoot", properties={"Transform": transform()}),
            mesh_component]))


def front_panel(mesh, center, width, height, texture, highlight=False):
    mesh.quad(center, (0, -width, 0), (0, 0, height), texture, highlight)


def build_dolly_zoom_sign():
    """A compact sign using the original station atlas, with no layered front panel."""
    sign = Mesh("Dolly_CompactStationSign")
    for y in (-.16, .16):
        sign.box((.01, y, .48), (.045, .045, .96), "wood_dark", omit_faces=("top",))
    sign.box((0, 0, 1.17), (.07, .50, .42), "wood_dark", omit_faces=("front",))
    front_panel(sign, (-.035, 0, 1.17), .50, .42, "station_name")
    return sign


def ground_height(x, y):
    return -.06 + .045 * math.sin(x * .6) * math.sin(y * .9)


def build_ground_and_track():
    meadow = Mesh("Meadow_Ground")
    for x in range(-4, 57):
        for y in range(-15, 16):
            points = [(px, py, ground_height(px, py))
                      for px, py in [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)]]
            meadow.face(points, "grass")
    track = Mesh("Track_BallastAndStones")
    for x in range(-4, 54):
        track.box((x + .5, TRACK_CENTER, -.035), (.998, 2.5, .11), "gravel")
    for _ in range(1250):
        x = RANDOM.uniform(-2, 31)
        y = TRACK_CENTER + RANDOM.uniform(-1.25, 1.25)
        radius = RANDOM.uniform(.018, .052)
        track.ellipsoid((x, y, .035), (radius * 1.4, radius, radius * .65),
                        RANDOM.choice(["gravel", "stone_light", "stone_dark"]), 5, 3)
    rails = Mesh("Track_SteelRailsAndSleepers")
    for index in range(91):
        x = -3 + index * .59
        rails.box((x, TRACK_CENTER, .074), (.21, 1.95, .10), "sleeper")
        for side in (-1, 1):
            y = TRACK_CENTER + side * .5335
            rails.box((x, y, .133), (.27, .20, .018), "rust")
            if x < 18:
                for offset in (-.078, .078):
                    rails.tube((x, y + offset, .14), (x, y + offset, .16), .019, "steel", 6)
    for side in (-1, 1):
        y = TRACK_CENTER + side * .5335
        rails.box((23.5, y, .15), (55, .135, .035), "rust")
        rails.box((23.5, y, .20), (55, .035, .09), "rust")
        rails.box((23.5, y, .25), (55, .067, .035), "rail")


def build_platform():
    structure = Mesh("Platform_TimberStructure")
    for x in [3.0 + index * 1.6 for index in range(7)]:
        for y in (-2.24, -.61):
            structure.box((x, y, .13), (.13, .13, .50), "wood_dark")
        structure.box((x, -1.43, .27), (.14, 1.83, .16), "wood_dark")
    for y in (-2.24, -.61):
        structure.box((7.9, y, .24), (10.1, .10, .16), "sleeper")
    boards = Mesh("Platform_WeatheredBoards")
    for index in range(40):
        x = 2.9 + index * .252
        boards.box((x, -1.43, .355), (.241, 1.91, .09),
                   RANDOM.choice(["wood", "wood", "wood_light", "wood_dark"]))
        for y in (-2.23, -.62):
            boards.tube((x, y, .401), (x, y, .406), .009, "steel", 6)
        if index % 4 == 0:
            boards.box((x + .04, -1.47, .401), (.004, RANDOM.uniform(.4, 1.1), .001), "wood_dark")
    sign = Mesh("Focus_KomorebiStationSign")
    for y in (-2.58, -1.89):
        sign.box((7.15, y, 1.03), (.07, .07, 1.72), "sign")
    sign.box((7.15, -2.235, 1.72), (.105, .88, .12), "cream")
    sign.box((7.15, -2.235, 1.39), (.08, .73, .55), "sign")
    front_panel(sign, (7.102, -2.235, 1.40), .67, .49, "station_name")
    sign.box((7.15, -2.235, .34), (.24, .86, .055), "wood_light")
    for y in (-2.58, -1.89):
        for z in (1.63, 1.14):
            sign.tube((7.088, y, z), (7.098, y, z), .011, "steel", 6)
    bench = Mesh("Background_BenchAndTimetable")
    for y in (-1.95, -1.15):
        bench.box((11.7, y, .62), (.7, .07, .44), "wood_dark")
    for index in range(4):
        bench.box((11.7, -1.90 + index * .22, .87), (.57, .20, .065), "wood_light")
    bench.box((12.02, -1.57, 1.13), (.055, 1.00, .32), "wood")
    front_panel(bench, (11.987, -1.57, 1.13), .88, .24, "wood_light")
    bench.box((12.40, -2.10, 1.30), (.07, .07, 1.83), "sign")
    front_panel(bench, (12.35, -2.10, 1.94), .38, .49, "timetable")


def flower(mesh, position, height, color):
    x, y, z = position
    tip = (x + height * .12, y, z + height)
    mesh.tube(position, tip, .006 if height < .6 else .009, "leaf", 4)
    mesh.leaf((x, y, z + height * .48), height * .38, height * .15,
              RANDOM.uniform(0, math.tau), "leaf")
    radius = .025 + height * .035
    for petal in range(5):
        angle = petal * math.tau / 5
        center = add(tip, (math.cos(angle) * radius * .72,
                           math.sin(angle) * radius * .72, 0))
        mesh.ellipsoid(center, (radius, radius * .70, radius * .22), color, 5, 3)
    mesh.ellipsoid(add(tip, (0, 0, .012)), (radius * .34,) * 3, "pollen", 5, 3)


def build_grass_and_flowers():
    for patch, bounds in enumerate([(-1, 8, -6, -2.65), (8, 18, -7, -2.7),
                                    (0, 12, 2.5, 6), (12, 29, 2.5, 8),
                                    (14, 32, -8, -2.8), (28, 48, -10, 10)]):
        mesh = Mesh(f"Meadow_GrassPatch_{patch + 1:02}")
        xmin, xmax, ymin, ymax = bounds
        for _ in range(640):
            x, y = RANDOM.uniform(xmin, xmax), RANDOM.uniform(ymin, ymax)
            if y > -2.6 and y < 2.5:
                continue
            z = ground_height(x, y)
            for _ in range(3):
                angle = RANDOM.uniform(0, math.tau)
                height = RANDOM.uniform(.06, .34)
                width = RANDOM.uniform(.012, .032)
                across = (-math.sin(angle) * width, math.cos(angle) * width, 0)
                root = (x + RANDOM.uniform(-.07, .07), y + RANDOM.uniform(-.07, .07), z)
                tip = add(root, (height * .3 * math.cos(angle), height * .3 * math.sin(angle), height))
                mesh.face([subtract(root, across), add(root, across), tip],
                          RANDOM.choice(["grass", "grass_light", "grass_dark", "leaf_gold"]),
                          two_sided=True)
    flowers = Mesh("Meadow_OrangeCosmos_LeftBank")
    for _ in range(115):
        x, y = RANDOM.uniform(4.2, 18.5), RANDOM.uniform(-4.8, -2.9)
        flower(flowers, (x, y, ground_height(x, y)), RANDOM.uniform(.45, 1.55),
               RANDOM.choice(["flower_red", "flower_red", "flower_coral"]))
    foreground = Mesh("Foreground_SoftFlowersAndTallGrass")
    for x, y, height in [(3.1, -.52, 1.38), (3.4, -.18, 1.70), (3.7, -.32, 1.55),
                          (3.8, -.95, 1.48), (4.4, -2.7, .82), (4.1, 2.0, .65)]:
        flower(foreground, (x, y, ground_height(x, y)), height, "flower_purple")
    for _ in range(48):
        x, y = RANDOM.uniform(4, 21), RANDOM.uniform(-5.5, 4.8)
        if -2.6 < y < 2.4:
            continue
        flower(flowers, (x, y, ground_height(x, y)), RANDOM.uniform(.13, .4), "flower_white")
    # Small opaque, scene-depth highlights become natural isolated background bokeh.
    glints = Mesh("Meadow_SunlitDewHighlights")
    for _ in range(105):
        x, y = RANDOM.uniform(6, 31), RANDOM.uniform(-5.5, 5.5)
        if -2.5 < y < 2.4:
            continue
        radius = RANDOM.uniform(.008, .028)
        glints.ellipsoid((x, y, RANDOM.uniform(.07, .32)), (radius,) * 3,
                         "glint", 6, 4, highlight=True)


def build_trees_and_hedges():
    for group, (xmin, xmax, ybase) in enumerate([(7, 22, -5.4), (17, 37, 6.5),
                                               (26, 47, -8.5), (34, 55, 3.5)]):
        mesh = Mesh(f"Garden_LeafyHedge_{group + 1:02}")
        for _ in range(10):
            x = RANDOM.uniform(xmin, xmax)
            y = ybase + RANDOM.uniform(-.8, .8)
            height = RANDOM.uniform(.8, 2.0)
            for _ in range(5):
                center = (x + RANDOM.uniform(-.7, .7), y + RANDOM.uniform(-.55, .55),
                          height * RANDOM.uniform(.35, .7))
                mesh.ellipsoid(center, (.65, .55, height * .48),
                               RANDOM.choice(["leaf", "leaf_dark", "leaf_light"]), 9, 5)
            for _ in range(42):
                mesh.leaf((x + RANDOM.uniform(-1.0, 1.0), y + RANDOM.uniform(-.8, .8),
                           RANDOM.uniform(.25, height)), RANDOM.uniform(.13, .29), .11,
                          RANDOM.uniform(0, math.tau),
                          RANDOM.choice(["leaf", "leaf_light", "leaf_gold"]))
    for index, (x, y) in enumerate([(21, -7), (28, -6), (34, -10), (41, -5.5),
                                   (22, 8), (29, 6.4), (36, 9), (43, 4.2),
                                   (46, -2.0), (51, 10), (52, -10)]):
        mesh = Mesh(f"Forest_BacklitTree_{index + 1:02}")
        height = RANDOM.uniform(6, 9)
        mesh.tube((x, y, -.1), (x + .23, y, height * .82), .17, "bark", 9)
        for branch in range(6):
            angle = branch * math.tau / 6 + .3
            end = (x + math.cos(angle) * 1.5, y + math.sin(angle) * 1.6, height * .85)
            mesh.tube((x, y, height * .53), end, .06, "bark", 6)
        for _ in range(14):
            mesh.ellipsoid((x + RANDOM.uniform(-2.1, 2.1), y + RANDOM.uniform(-1.8, 1.8),
                            height + RANDOM.uniform(-1.3, 1.0)),
                           (RANDOM.uniform(1.0, 1.8), RANDOM.uniform(.9, 1.7), 1.3),
                           RANDOM.choice(["leaf", "leaf_dark", "leaf_light", "leaf_gold"]), 10, 6)
    backdrop = Mesh("Forest_DistantHillsAndSky")
    front_panel(backdrop, (85, 0, 25), 220, 110, "sky")
    for index in range(9):
        backdrop.ellipsoid((66 + RANDOM.uniform(-2, 7), -48 + index * 12, 4),
                            (9, 13, RANDOM.uniform(9, 15)), "mountain", 12, 7)


def panel_with_window(draw_panel, bounds, window, surround_texture):
    left, right, bottom, top = bounds
    window_left, window_right, window_bottom, window_top = window
    # Glass and its surround share edges only, so there is no second surface behind the pane.
    draw_panel(left, right, bottom, window_bottom, surround_texture)
    draw_panel(left, right, window_top, top, surround_texture)
    draw_panel(left, window_left, window_bottom, window_top, surround_texture)
    draw_panel(window_right, right, window_bottom, window_top, surround_texture)
    draw_panel(window_left, window_right, window_bottom, window_top, "glass")


def build_train_front_window(mesh, front_x, width_bounds, height_bounds, window, texture):
    def draw_panel(left, right, bottom, top, panel_texture):
        front_panel(mesh, (front_x, (left + right) * .5, (bottom + top) * .5),
                    right - left, top - bottom, panel_texture)

    panel_with_window(draw_panel, (*width_bounds, *height_bounds), window, texture)


def build_train_side_windows(mesh, x, y, side, side_bottom, side_top):
    def draw_panel(first, last, bottom, top, texture):
        mesh.quad(((first + last) * .5, y + side * 1.07, (bottom + top) * .5),
                  (-side * (last - first), 0, 0), (0, 0, top - bottom), texture)

    # Replace each side wall with adjacent panels rather than overlapping glass and trim.
    side_start, side_end = x + .02, x + 5.78
    first_panel_start = x + .3075
    draw_panel(side_start, first_panel_start, side_bottom, side_top, "cream")
    for index in range(6):
        window_center = x + .70 + index * .87
        panel_start = first_panel_start + index * .87
        panel_end = panel_start + .87 if index < 5 else side_end
        divider_end = panel_start + .045
        draw_panel(panel_start, divider_end, 1.815, 2.565, "train_dark")
        draw_panel(panel_start, divider_end, side_bottom, 1.815, "cream")
        draw_panel(panel_start, divider_end, 2.565, side_top, "cream")
        panel_with_window(draw_panel, (divider_end, panel_end, side_bottom, side_top),
                          (window_center - .345, window_center + .345, 1.87, 2.51), "cream")


def build_railcar():
    mesh = Mesh("Background_BlueCreamLocalTrain")
    x, y = TRAIN_FRONT, TRACK_CENTER
    cabin_bottom, roof_bottom = 1.715, 2.57
    cabin_front = x + .02
    cabin_width = 2.14
    mesh.box((x + 2.9, y, 1.1075), (5.8, 2.18, 1.075), "train_blue", omit_faces=("top",))
    mesh.box((x + 2.9, y, (cabin_bottom + roof_bottom) * .5),
             (5.76, cabin_width, roof_bottom - cabin_bottom), "cream",
             omit_faces=("front", "sides", "top", "bottom"))
    mesh.box((x + 2.9, y, 1.68), (5.81, 2.19, .07), "train_dark")
    # A curved roof cross-section is extruded along the car's length.
    profile = [(y - 1.15, roof_bottom), (y - 1.12, 2.78), (y - .85, 2.94),
               (y - .45, 3.00), (y + .45, 3.00), (y + .85, 2.94),
               (y + 1.12, 2.78), (y + 1.15, roof_bottom)]
    mesh.face([(x - .08, py, pz) for py, pz in profile], "roof")
    mesh.face([(x + 5.88, py, pz) for py, pz in reversed(profile)], "roof")
    for first, second in zip(profile, profile[1:]):
        mesh.face([(x - .08, *first), (x + 5.88, *first),
                   (x + 5.88, *second), (x - .08, *second)], "roof")
    # Close the lip seen from the platform below. The cabin ends at this surface.
    mesh.quad((x + 2.9, y, roof_bottom), (5.96, 0, 0), (0, -2.30, 0), "roof")

    def add_cabin_panel(left, right, bottom, top):
        front_panel(mesh, (cabin_front, (left + right) * .5, (bottom + top) * .5),
                    right - left, top - bottom, "cream")

    # Leave actual openings behind the projecting windows and door.
    for left, right in ((-1.07, -1.035), (-.245, -.17), (.17, .245), (1.035, 1.07)):
        add_cabin_panel(y + left, y + right, cabin_bottom, roof_bottom)
    for offset in (-.64, .64):
        add_cabin_panel(y + offset - .395, y + offset + .395, cabin_bottom, 1.83)
    add_cabin_panel(y - .17, y + .17, 2.55, roof_bottom)

    for offset in (-.64, .64):
        window_y = y + offset
        mesh.box((x - .0235, window_y, 2.20), (.087, .79, .74), "train_dark",
                 omit_faces=("front", "back", "top"))
        build_train_front_window(mesh, x - .067, (window_y - .395, window_y + .395),
                                 (1.83, 2.57), (window_y - .34, window_y + .34, 1.88, 2.52),
                                 "train_dark")
        mesh.tube((x - .088, window_y - .16, 1.94),
                  (x - .088, window_y + .13, 2.16), .011, "steel", 5)
    # The door stops below the roof cap and extends into the shell without a flush rear face.
    mesh.box((x - .029, y, 1.99), (.118, .34, 1.12), "cream", omit_faces=("front", "back"))
    build_train_front_window(mesh, x - .088, (y - .17, y + .17), (1.43, 2.55),
                             (y - .13, y + .13, 2.05, 2.47), "cream")
    front_panel(mesh, (x - .095, y, 2.69), .92, .17, "train_destination")
    front_panel(mesh, (x - .081, y + .44, 1.17), .39, .48, "train_number")
    for offset in (-.82, .82):
        mesh.tube((x - .10, y + offset, 1.55), (x - .045, y + offset, 1.55), .09, "steel", 12)
        mesh.ellipsoid((x - .112, y + offset, 1.55), (.014, .065, .065), "glint", 10, 6, True)
        mesh.ellipsoid((x - .085, y + offset, .91), (.012, .042, .042), "flower_red", 8, 4)
    mesh.box((x - .12, y, .63), (.18, 2.01, .15), "train_dark")
    mesh.box((x - .25, y, .40), (.35, .17, .14), "steel")
    for wheel_x in (x + 1.1, x + 4.65):
        mesh.box((wheel_x, y, .48), (1.0, 1.40, .24), "black")
        for side in (-1, 1):
            wheel_y = y + side * .62
            mesh.tube((wheel_x, wheel_y - .09, .45),
                      (wheel_x, wheel_y + .09, .45), .20, "steel", 12)
    for side in (-1, 1):
        build_train_side_windows(mesh, x, y, side, cabin_bottom, roof_bottom)


def build_character():
    # A display actor avoids player possession and character movement/gravity.
    skeletal = component("USkeletalMeshComponent", "SkeletalMeshComponent", "DefaultSceneRoot", {
        "Transform": transform(scale=(.01, .01, .01)),
        "SkeletalMesh": "FBX/Kurisu/Kurisu.fbx.bin", "AnimBlueprintPath": "",
        "AnimInstance": None, "BlendSpaceInput": 0.0, "bAutoActivate": True,
        "bVisible": True, "bHiddenInGame": False, "bCastShadow": True,
        "bReceiveShadow": True, "bBlockComponent": False, "bGenerateOverlapEvents": False})
    skeletal["BonePose"] = []
    skeletal["OverrideMaterials"] = []
    skeletal["SingleAnimation"] = {"Asset": "FBX/Kurisu_Idle/Kurisu_Idle.anim.bin",
                                  "Looping": True, "PlayRate": .65}
    ACTORS.append(actor("AActor", "SS_Focus_Kurisu_ExistingCharacter", "DefaultSceneRoot", [
        component("USceneComponent", "DefaultSceneRoot", properties={
            "Transform": transform(HERO_POSITION, (0, 174, 0))}), skeletal]))


def packed_color(red, green, blue):
    return red | green << 8 | blue << 16 | 255 << 24


def build_lighting():
    reference = json.loads((SCENES / "DOFTest.scene").read_text(encoding="utf-8-sig"))
    originals = reference["World"]["PersistentLevel"]["Actors"]
    for class_name, component_class, name, intensity, color in [
        ("AAmbientLightActor", "UAmbientLightComponent", "SS_SoftSkyFill", .55, (221, 231, 205)),
        ("ADirectionalLightActor", "UDirectionalLightComponent", "SS_WarmAfternoonSun", 1.65, (255, 237, 196)),
    ]:
        light = copy.deepcopy(next(item for item in originals if item["Class"] == class_name))
        light["Name"] = name
        for item in light["Components"]:
            if item["Class"] == component_class:
                properties = item["Properties"]
                properties["Intensity"] = intensity
                properties["LightColor"] = packed_color(*color)
                properties["Transform"] = transform((0, 0, 12), (38, 28, 0))
                if component_class == "UDirectionalLightComponent":
                    properties.update({"bCastShadows": True, "AngularRadiusDegrees": 1.2,
                                       "DynamicShadowDistanceMovableLight": 65.0,
                                       "DynamicShadowCascades": 4})
        ACTORS.append(light)


def camera(location, rotation, focal_length, name):
    reference = json.loads((SCENES / "DOFTest.scene").read_text(encoding="utf-8-sig"))
    originals = reference["World"]["PersistentLevel"]["Actors"]
    result = copy.deepcopy(next(item for item in originals if item["Class"] == "ADefaultPawn"))
    result["Name"] = name
    pitch, yaw = (math.radians(value) for value in rotation[:2])
    forward = (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch))
    # The renderer focuses using forward/view depth rather than Euclidean distance.
    target = (HERO_POSITION[0] - .06, HERO_POSITION[1], HERO_POSITION[2] + 1.42)
    focus = sum(a * b for a, b in zip(subtract(target, location), forward))
    for item in result["Components"]:
        if item["Class"] == "UStaticMeshComponent":
            item["Properties"]["Transform"] = transform(location, rotation)
        elif item["Class"] == "UCameraComponent":
            item["Properties"].update({
                "Transform": transform(),
                "FieldOfView": math.degrees(2 * math.atan(24 / (2 * focal_length))),
                "LensSettings": {"bUseFocalLength": True, "FocalLengthMm": focal_length,
                                 "SensorHeightMm": 24.0},
                "DepthOfFieldSettings": {"bEnabled": True, "FStop": 1.2, "MinFStop": 1.2,
                                         "ApertureBladeCount": 9, "FocalDistanceMeters": focus,
                                         "DepthBlurKmFor50Percent": 0.0,
                                         "DepthBlurRadiusFractionOfHeight": 0.0},
                "bAutoFocusEnabled": True,
                "AutoFocusTargetActorName": "SS_Focus_Kurisu_ExistingCharacter",
                "AutoFocusTargetBoneName": "Bip001-Head",
                "AutoFocusTargetOffset": {"X": 0.0, "Y": 0.0, "Z": .04},
                "AutoFocusFallbackOffset": {"X": -.06, "Y": 0.0, "Z": 1.42},
                "AutoFocusInterpSpeed": 8.0,
                "bUsePawnControlRotation": False})
    return result, focus


def main():
    for relative in ("FBX/Kurisu/Kurisu.fbx.bin", "FBX/Kurisu/Textures/192.png",
                     "FBX/Kurisu_Idle/Kurisu_Idle.anim.bin"):
        if not (CONTENT / relative).is_file():
            raise FileNotFoundError(CONTENT / relative)
    DESTINATION.mkdir(parents=True, exist_ok=True)
    build_atlas()
    (DESTINATION / "Station.mtl").write_text(
        "# Opaque scene-depth surfaces; highlights use HDR diffuse gain.\n"
        "newmtl Surface\nKd 1 1 1\nd 1\nmap_Kd StationAtlas.png\n\n"
        "newmtl Highlight\nKd 4 4 4\nd 1\nmap_Kd StationAtlas.png\n", encoding="ascii")
    build_ground_and_track()
    build_platform()
    build_grass_and_flowers()
    build_trees_and_hedges()
    build_railcar()
    build_character()
    build_lighting()
    for mesh in MESHES:
        mesh.save()
    build_dolly_zoom_sign().save(add_scene_actor=False)

    cameras = [
        ("DOF_SunlitStation.scene", (0, 0, 2.2), (6, -7, 0), 75.0, "SS_PlayCamera_75mm_KurisuFocus"),
    ]
    for filename, location, rotation, focal_length, name in cameras:
        scene_camera, focus = camera(location, rotation, focal_length, name)
        document = {"Version": 4, "World": {"Properties": {}, "PersistentLevel": {
            "Properties": {}, "Actors": [scene_camera, *ACTORS]}}}
        (SCENES / filename).write_text(json.dumps(document, ensure_ascii=False, indent=4) + "\n",
                                      encoding="utf-8")
        print(f"{filename}: {focal_length:g}mm, f/1.2, focus {focus:.4f}m")
    triangles = sum(len(points) - 2 for mesh in MESHES for faces in mesh.faces.values()
                    for points, _, _ in faces)
    print(f"Created {len(MESHES)} meshes, {len(ACTORS) + 1} actors/scene, {triangles:,} environment triangles")


if __name__ == "__main__":
    main()
