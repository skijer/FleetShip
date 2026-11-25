"""Water class of a spawn row: submerged, at the surface or dry, from the scene collision."""

from scene_resources import WATER_BOX_ALL_ROOMS

ROW_SUBMERGED = 8
ROW_SURFACE = 16
SUBMERGED_DEPTH = 30
SHALLOW_WATER_DEPTH = 30
ABOVE_SURFACE_MARGIN = 30
FLOOR_PROBE_HEIGHT = 30
CAVITY_CLEARANCE = 10
MIN_NORMAL_Y = 0.01


def poly_height_at(poly, x, z):
    nx, ny, nz = poly.normal
    if abs(ny) < MIN_NORMAL_Y or not is_inside_xz(poly.vertices, x, z):
        return None
    return -(nx * x + nz * z + poly.dist) / ny


def is_inside_xz(vertices, x, z):
    (ax, _, az), (bx, _, bz), (cx, _, cz) = vertices
    edge_ab = (bx - ax) * (z - az) - (bz - az) * (x - ax)
    edge_bc = (cx - bx) * (z - bz) - (cz - bz) * (x - bx)
    edge_ca = (ax - cx) * (z - cz) - (az - cz) * (x - cx)
    has_negative = edge_ab < 0 or edge_bc < 0 or edge_ca < 0
    has_positive = edge_ab > 0 or edge_bc > 0 or edge_ca > 0
    return not (has_negative and has_positive)


def solid_heights_at(polys, x, z):
    heights = []
    for poly in polys:
        if poly.ignores_entities:
            continue
        height = poly_height_at(poly, x, z)
        if height is not None:
            heights.append((height, poly.normal[1] > 0))
    return heights


def find_floor(heights, y):
    floors = [height for height, faces_up in heights if faces_up and height <= y + FLOOR_PROBE_HEIGHT]
    return max(floors, default=None)


def is_cut_off(heights, y, surface):
    return any(faces_up and y + CAVITY_CLEARANCE < height < surface for height, faces_up in heights)


def find_surface(boxes, room, x, z, lowest_surface):
    surfaces = [box.y_surface for box in boxes
                if not box.is_inactive and box.room in (room, WATER_BOX_ALL_ROOMS)
                and box.contains_xz(x, z) and box.y_surface >= lowest_surface]
    return min(surfaces, default=None)


def classify_water(position, room, geometry):
    x, y, z = position
    heights = solid_heights_at(geometry.polys, x, z)
    floor = find_floor(heights, y)
    deep_from = floor + SHALLOW_WATER_DEPTH if floor is not None else float("-inf")
    surface = find_surface(geometry.water_boxes, room, x, z, max(deep_from, y - ABOVE_SURFACE_MARGIN))
    if surface is None or is_cut_off(heights, y, surface):
        return 0
    return ROW_SUBMERGED if surface - y > SUBMERGED_DEPTH else ROW_SURFACE
