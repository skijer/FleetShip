"""Binary scene, room and collision resources of Ship/2Ship .o2r archives."""

import struct
import zipfile
from dataclasses import dataclass, field

RESOURCE_HEADER_SIZE = 0x40
NO_ROOM = -1

CMD_START_POSITIONS = 0x00
CMD_ACTORS = 0x01
CMD_CS_CAMERA = 0x02
CMD_COLLISION = 0x03
CMD_ROOMS = 0x04
CMD_WIND = 0x05
CMD_ENTRANCES = 0x06
CMD_SPECIAL_OBJECTS = 0x07
CMD_ROOM_BEHAVIOR = 0x08
CMD_MESH = 0x0A
CMD_OBJECTS = 0x0B
CMD_LIGHTS = 0x0C
CMD_PATHWAYS = 0x0D
CMD_TRANSITION_ACTORS = 0x0E
CMD_LIGHTING = 0x0F
CMD_TIME = 0x10
CMD_SKYBOX = 0x11
CMD_SKYBOX_MODIFIER = 0x12
CMD_EXITS = 0x13
CMD_END = 0x14
CMD_SOUND = 0x15
CMD_ECHO = 0x16
CMD_CUTSCENES = 0x17
CMD_ALTERNATE_HEADERS = 0x18
CMD_CAMERA_SETTINGS = 0x19
CMD_ANIMATED_MATERIALS = 0x1A
CMD_ACTOR_CUTSCENES = 0x1B
CMD_MINIMAP_LIST = 0x1C
CMD_MINIMAP_CHESTS = 0x1E
CMD_CUTSCENES_MM = 0x1F

ACTOR_ENTRY_SIZE = 16
TRANSITION_ACTOR_SIZE = 16
LIGHT_SIZE = 13
LIGHTING_SETTING_SIZE = 22
MINIMAP_ENTRY_SIZE = 10
ACTOR_CUTSCENE_SIZE = 16
WATER_BOX_ALL_ROOMS = 0x3F


class ResourceReader:
    def __init__(self, data):
        self.data = data
        self.position = RESOURCE_HEADER_SIZE

    def read(self, fmt):
        size = struct.calcsize(fmt)
        values = struct.unpack_from("<" + fmt, self.data, self.position)
        self.position += size
        return values if len(values) > 1 else values[0]

    def skip(self, size):
        self.position += size

    def read_string(self):
        length = self.read("i")
        text = self.data[self.position:self.position + length].decode("utf-8")
        self.position += length
        return text

    def is_at_end(self):
        return self.position == len(self.data)


@dataclass
class ActorEntry:
    actor_id: int
    pos: tuple
    rot: tuple
    params: int


@dataclass
class TransitionActor:
    front_room: int
    back_room: int
    actor_id: int
    params: int


@dataclass
class WaterBox:
    room: int
    x_min: int = 0
    y_surface: int = 0
    z_min: int = 0
    x_length: int = 0
    z_length: int = 0
    is_inactive: bool = False

    def contains_xz(self, x, z):
        return self.x_min < x < self.x_min + self.x_length and self.z_min < z < self.z_min + self.z_length


@dataclass
class CollisionPoly:
    vertices: tuple
    normal: tuple
    dist: float
    ignores_entities: bool


@dataclass
class CollisionGeometry:
    polys: list
    water_boxes: list


@dataclass
class SceneHeader:
    actors: list = None
    start_positions: list = None
    entrances: list = None
    transition_actors: list = None
    rooms: list = None
    collision_path: str = None
    alternate_headers: list = None
    commands: list = field(default_factory=list)


class SceneDialect:
    """Game-specific differences in command layouts."""

    def __init__(self, is_majoras_mask):
        self.is_majoras_mask = is_majoras_mask

    def skip_cs_camera(self, reader):
        if not self.is_majoras_mask:
            reader.skip(5)
            return
        for _ in range(reader.read("I")):
            _setting, count = reader.read("HH")
            reader.skip(count * 6)

    def skip_room_behavior(self, reader):
        reader.skip(6 if self.is_majoras_mask else 5)

    def skip_camera_settings(self, reader):
        reader.skip(0 if self.is_majoras_mask else 5)

    def read_cutscenes_mm(self, reader):
        for _ in range(reader.read("B")):
            reader.read_string()
            reader.skip(4)


def read_actor_entries(reader):
    entries = []
    for _ in range(reader.read("I")):
        actor_id, x, y, z, rot_x, rot_y, rot_z, params = reader.read("Hhhhhhhh")
        entries.append(ActorEntry(actor_id, (x, y, z), (rot_x, rot_y, rot_z), params & 0xFFFF))
    return entries


def read_transition_actors(reader, dialect):
    sides_format = "BBBB" if dialect.is_majoras_mask else "bBbB"
    entries = []
    for _ in range(reader.read("I")):
        front_room, _front_effects, back_room, _back_effects = reader.read(sides_format)
        actor_id, _x, _y, _z, _rot_y, params = reader.read("hhhhhH")
        entries.append(TransitionActor(normalize_room(front_room), normalize_room(back_room), actor_id, params))
    return entries


def normalize_room(room):
    return NO_ROOM if room in (-1, 0xFF) else room


def read_entrances(reader, dialect):
    entry_format = "bb" if dialect.is_majoras_mask else "bB"
    return [reader.read(entry_format) for _ in range(reader.read("I"))]


def read_rooms(reader):
    rooms = []
    for _ in range(reader.read("i")):
        rooms.append(reader.read_string())
        reader.skip(8)
    return rooms


def skip_counted(reader, entry_size, extra_header=0):
    count = reader.read("I")
    reader.skip(extra_header + count * entry_size)


def skip_mesh(reader):
    reader.skip(1)
    mesh_type = reader.read("b")
    polygon_count = 1 if mesh_type == 1 else reader.read("B")
    for _ in range(polygon_count):
        if mesh_type == 1:
            skip_background_mesh(reader)
        else:
            reader.skip(1 + (8 if mesh_type == 2 else 0))
            reader.read_string()
            reader.read_string()


def skip_background_mesh(reader):
    reader.skip(1)
    reader.read_string()
    reader.read_string()
    for _ in range(reader.read("I")):
        reader.skip(3)
        reader.read_string()
        reader.skip(18)
    reader.skip(1)
    reader.read_string()
    reader.read_string()


def parse_scene_header(data, dialect):
    reader = ResourceReader(data)
    header = SceneHeader()
    for _ in range(reader.read("I")):
        command = reader.read("i")
        header.commands.append(command)
        parse_command(command, reader, header, dialect)
    if not reader.is_at_end() or header.commands[-1] != CMD_END:
        raise ValueError("scene command stream does not end at the end marker")
    return header


def parse_command(command, reader, header, dialect):
    if command == CMD_ACTORS:
        header.actors = read_actor_entries(reader)
    elif command == CMD_START_POSITIONS:
        header.start_positions = read_actor_entries(reader)
    elif command == CMD_ENTRANCES:
        header.entrances = read_entrances(reader, dialect)
    elif command == CMD_TRANSITION_ACTORS:
        header.transition_actors = read_transition_actors(reader, dialect)
    elif command == CMD_ROOMS:
        header.rooms = read_rooms(reader)
    elif command == CMD_COLLISION:
        header.collision_path = reader.read_string()
    elif command == CMD_ALTERNATE_HEADERS:
        header.alternate_headers = [reader.read_string() for _ in range(reader.read("I"))]
    else:
        skip_unused_command(command, reader, dialect)


def skip_unused_command(command, reader, dialect):
    fixed_sizes = {
        CMD_WIND: 4,
        CMD_SPECIAL_OBJECTS: 3,
        CMD_TIME: 3,
        CMD_SKYBOX: 4,
        CMD_SKYBOX_MODIFIER: 2,
        CMD_SOUND: 3,
        CMD_ECHO: 1,
        CMD_END: 0,
    }
    counted_sizes = {
        CMD_OBJECTS: 2,
        CMD_LIGHTS: LIGHT_SIZE,
        CMD_LIGHTING: LIGHTING_SETTING_SIZE,
        CMD_EXITS: 2,
        CMD_ACTOR_CUTSCENES: ACTOR_CUTSCENE_SIZE,
        CMD_MINIMAP_CHESTS: MINIMAP_ENTRY_SIZE,
    }
    if command in fixed_sizes:
        reader.skip(fixed_sizes[command])
    elif command in counted_sizes:
        skip_counted(reader, counted_sizes[command])
    elif command == CMD_MINIMAP_LIST:
        skip_counted(reader, MINIMAP_ENTRY_SIZE, extra_header=2)
    elif command in (CMD_CUTSCENES, CMD_ANIMATED_MATERIALS):
        reader.read_string()
    elif command == CMD_PATHWAYS:
        for _ in range(reader.read("I")):
            reader.read_string()
    elif command == CMD_MESH:
        skip_mesh(reader)
    elif command == CMD_CS_CAMERA:
        dialect.skip_cs_camera(reader)
    elif command == CMD_ROOM_BEHAVIOR:
        dialect.skip_room_behavior(reader)
    elif command == CMD_CAMERA_SETTINGS:
        dialect.skip_camera_settings(reader)
    elif command == CMD_CUTSCENES_MM:
        dialect.read_cutscenes_mm(reader)
    else:
        raise ValueError(f"unknown scene command 0x{command:02X}")


POLY_VERTEX_MASK = 0x1FFF
POLY_IGNORE_ENTITIES = 0x4000
NORMAL_SCALE = 1.0 / 0x7FFF
WATER_BOX_INACTIVE = 0x80000


def read_polys(reader, vertices):
    polys = []
    for _ in range(reader.read("I")):
        _type, vertex_a, vertex_b, vertex_c, nx, ny, nz, dist = reader.read("HHHHhhhh")
        corners = tuple(vertices[index & POLY_VERTEX_MASK] for index in (vertex_a, vertex_b, vertex_c))
        normal = (nx * NORMAL_SCALE, ny * NORMAL_SCALE, nz * NORMAL_SCALE)
        polys.append(CollisionPoly(corners, normal, dist, bool(vertex_a & POLY_IGNORE_ENTITIES)))
    return polys


def read_collision(data):
    reader = ResourceReader(data)
    reader.skip(12)
    vertices = [reader.read("hhh") for _ in range(reader.read("i"))]
    polys = read_polys(reader, vertices)
    reader.skip(reader.read("I") * 8)
    reader.skip(reader.read("I") * 8)
    reader.skip(reader.read("i") * 6)
    boxes = []
    for _ in range(reader.read("i")):
        x_min, y_surface, z_min, x_length, z_length = reader.read("hhhhh")
        properties = reader.read("I")
        boxes.append(WaterBox((properties >> 13) & 0x3F, x_min, y_surface, z_min, x_length, z_length,
                              bool(properties & WATER_BOX_INACTIVE)))
    if not reader.is_at_end():
        raise ValueError("collision header has trailing data")
    return CollisionGeometry(polys, boxes)


def read_water_boxes(data):
    return read_collision(data).water_boxes


class ResourceArchive:
    def __init__(self, path):
        self.zip_file = zipfile.ZipFile(path)

    def read(self, name):
        return self.zip_file.read(name)

    def contains(self, name):
        return name in self.zip_file.NameToInfo
