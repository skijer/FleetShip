"""Scene, actor and entrance tables read from the decomp sources of Ship and 2Ship."""

import re
from dataclasses import dataclass
from pathlib import Path

SCENE_ROW = re.compile(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_SCENE\(([^)]*)\)")
ACTOR_ROW = re.compile(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_ACTOR\w*\(\s*(\w+)\s*,\s*(\w+)")
OOT_ENTRANCE_ROW = re.compile(
    r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_ENTRANCE\(\s*\w+\s*,\s*(\w+)\s*,\s*(\d+)")
PROFILE_BLOCK = re.compile(r"(?:ActorInit|ActorProfile)\s+\w+\s*=\s*\{(.*?)\};", re.DOTALL)
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
MM_ENTRANCE_ARRAY = re.compile(r"EntranceTableEntry\s+(\w+)\[\]\s*=\s*\{(.*?)\};", re.DOTALL)
MM_ENTRANCE_TABLE = re.compile(r"EntranceTableEntry\*\s+(\w+)\[\]\s*=\s*\{(.*?)\};", re.DOTALL)
MM_ENTRANCE_ENTRY = re.compile(r"\{\s*(-?\w+)\s*,\s*(\d+)\s*,\s*0x[0-9A-Fa-f]+\s*\}")
MM_SCENE_ENTRANCE_ROW = re.compile(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*SCENE_ENTRANCE\((\w+)\s*,")
MM_ENTRANCE_SCENE_SHIFT = 9
MM_ENTRANCE_INDEX_SHIFT = 4


@dataclass
class SceneInfo:
    scene_id: int
    directory_name: str
    enum_name: str


@dataclass
class ActorInfo:
    actor_id: int
    enum_name: str
    overlay_name: str
    category: str


@dataclass
class EntranceInfo:
    entrance: int
    scene_id: int
    spawn: int


def read_scene_table(path):
    scenes = []
    for index, arguments in SCENE_ROW.findall(Path(path).read_text(encoding="utf-8")):
        names = [argument.strip() for argument in arguments.split(",")]
        enum_name = next(name for name in names if name.startswith("SCENE_"))
        scenes.append(SceneInfo(int(index, 16), names[0], enum_name))
    return scenes


def read_actor_categories(actors_directory):
    categories = {}
    for source in Path(actors_directory).glob("*/*.c"):
        text = source.read_text(encoding="utf-8", errors="replace")
        for block in PROFILE_BLOCK.findall(text):
            fields = [field.strip() for field in BLOCK_COMMENT.sub("", block).split(",")]
            if len(fields) > 1 and fields[1].startswith("ACTORCAT_"):
                categories[fields[0]] = fields[1]
    return categories


def read_actor_table(table_path, actors_directory):
    categories = read_actor_categories(actors_directory)
    actors = []
    for index, overlay_name, enum_name in ACTOR_ROW.findall(Path(table_path).read_text(encoding="utf-8")):
        actors.append(ActorInfo(int(index, 16), enum_name, overlay_name, categories.get(enum_name)))
    return actors


def read_oot_entrances(table_path, scenes):
    scene_ids = {scene.enum_name: scene.scene_id for scene in scenes}
    text = Path(table_path).read_text(encoding="utf-8")
    return [
        EntranceInfo(int(index, 16), scene_ids[scene_name], int(spawn))
        for index, scene_name, spawn in OOT_ENTRANCE_ROW.findall(text)
        if scene_name in scene_ids
    ]


def read_mm_entrance_arrays(text, scene_ids):
    arrays = {}
    for name, body in MM_ENTRANCE_ARRAY.findall(text):
        arrays[name] = [(scene_ids[scene_name.lstrip("-")], int(spawn))
                        for scene_name, spawn in MM_ENTRANCE_ENTRY.findall(body)
                        if scene_name.lstrip("-") in scene_ids]
    return arrays


def read_mm_entrances(scene_table_source, scenes):
    scene_ids = {scene.enum_name: scene.scene_id for scene in scenes}
    text = Path(scene_table_source).read_text(encoding="utf-8")
    arrays = read_mm_entrance_arrays(text, scene_ids)
    tables = {name: re.findall(r"\w+", body) for name, body in MM_ENTRANCE_TABLE.findall(text)}
    entrances = []
    for table_index, table_name in MM_SCENE_ENTRANCE_ROW.findall(text):
        for entrance_index, array_name in enumerate(tables[table_name]):
            if not arrays[array_name]:
                continue
            scene_id, spawn = arrays[array_name][0]
            entrance = (int(table_index, 16) << MM_ENTRANCE_SCENE_SHIFT) | (entrance_index << MM_ENTRANCE_INDEX_SHIFT)
            entrances.append(EntranceInfo(entrance, scene_id, spawn))
    return entrances
