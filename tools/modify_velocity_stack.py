import bpy
import bmesh
import math
import os
import shutil
import struct
import sys
import zipfile
import xml.etree.ElementTree as ET
from mathutils import Matrix, Vector


SOURCE_3MF = r"C:\Users\miner\OneDrive\Desktop\main folder\3d prints\velocity stack with flow straightener,maf,iat bung v1.0.3mf"
OUTPUT_DIR = r"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\velocity_stack_work"
VARIANTS = {
    "smooth": {
        "label": "A_smooth_gradual_control",
        "rows": [],
        "depth": 0.0,
    },
    "lip": {
        "label": "B_shallow_dimples_mouth_half",
        "rows": [-71.5, -67.5, -63.5, -59.5, -55.5],
        "depth": 0.55,
    },
    "full": {
        "label": "C_shallow_dimples_full_bell",
        "rows": [
            -71.5, -67.5, -63.5, -59.5, -55.5,
            -51.5, -47.5, -43.5, -39.5, -35.5,
        ],
        "depth": 0.55,
    },
    "deep": {
        "label": "D_deep_dimples_full_bell",
        "rows": [
            -71.5, -67.5, -63.5, -59.5, -55.5,
            -51.5, -47.5, -43.5, -39.5, -35.5,
        ],
        "depth": 0.75,
    },
    "rounded_smooth": {
        "label": "E_rounded_lip_smooth_control",
        "rows": [],
        "depth": 0.0,
        "rounded_lip": True,
    },
    "rounded_full": {
        "label": "F_rounded_lip_shallow_dimples",
        "rows": [
            -71.5, -67.5, -63.5, -59.5, -55.5,
            -51.5, -47.5, -43.5, -39.5, -35.5,
        ],
        "depth": 0.55,
        "rounded_lip": True,
    },
    "map_clip": {
        "label": "G_rounded_dimpled_MAP_snap_mount",
        "rows": [
            -71.5, -67.5, -63.5, -59.5, -55.5,
            -51.5, -47.5, -43.5, -39.5, -35.5,
        ],
        "depth": 0.55,
        "rounded_lip": True,
        "map_mount": True,
    },
}
VARIANT_KEY = "full"
if "--" in sys.argv:
    user_args = sys.argv[sys.argv.index("--") + 1:]
    if user_args:
        VARIANT_KEY = user_args[0].lower()
if VARIANT_KEY not in VARIANTS:
    raise ValueError(f"Unknown variant {VARIANT_KEY}; choose from {tuple(VARIANTS)}")
VARIANT = VARIANTS[VARIANT_KEY]
OUTPUT_3MF = os.path.join(OUTPUT_DIR, f"{VARIANT['label']}.3mf")
OUTPUT_STL = os.path.join(OUTPUT_DIR, f"{VARIANT['label']}.stl")
OUTPUT_BLEND = os.path.join(OUTPUT_DIR, f"{VARIANT['label']}.blend")
MODEL_ENTRY = "3D/Objects/object_1.model"
CORE_NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
PROD_NS = "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"
NS = {"m": CORE_NS}


def component_matrix(values):
    a, b, c, d, e, f, g, h, i, tx, ty, tz = values
    return Matrix((
        (a, d, g, tx),
        (b, e, h, ty),
        (c, f, i, tz),
        (0.0, 0.0, 0.0, 1.0),
    ))


def parse_source():
    with zipfile.ZipFile(SOURCE_3MF) as archive:
        root_model = ET.fromstring(archive.read("3D/3dmodel.model"))
        object_model = ET.fromstring(archive.read(MODEL_ENTRY))
    parent = root_model.find(".//m:object[@id='3']", NS)
    components = {}
    for component in parent.findall("./m:components/m:component", NS):
        object_id = component.attrib["objectid"]
        values = tuple(float(v) for v in component.attrib.get(
            "transform", "1 0 0 0 1 0 0 0 1 0 0 0"
        ).split())
        components[object_id] = component_matrix(values)
    return object_model, components


def mesh_data(model_root, object_id):
    obj = model_root.find(f".//m:object[@id='{object_id}']", NS)
    vertices = [
        Vector((float(v.attrib["x"]), float(v.attrib["y"]), float(v.attrib["z"])))
        for v in obj.findall("./m:mesh/m:vertices/m:vertex", NS)
    ]
    faces = [
        (int(t.attrib["v1"]), int(t.attrib["v2"]), int(t.attrib["v3"]))
        for t in obj.findall("./m:mesh/m:triangles/m:triangle", NS)
    ]
    return vertices, faces


def create_object(name, vertices, faces, transform):
    transformed = [(transform @ v.to_4d()).to_3d() for v in vertices]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(transformed, [], faces)
    mesh.validate(verbose=True)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def soften_bell(obj):
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)

    # Split the original long, straight facets into profile rings. Moving these
    # rings creates a smooth, earlier flare while retaining both end positions.
    for z_value in (-68.0, -62.0, -56.0, -50.0, -44.0, -38.0, -32.0):
        bmesh.ops.bisect_plane(
            bm,
            geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
            plane_co=Vector((0.0, 0.0, z_value)),
            plane_no=Vector((0.0, 0.0, 1.0)),
            clear_inner=False,
            clear_outer=False,
            use_snap_center=False,
        )

    moved = 0
    for vert in bm.verts:
        x, y, z = vert.co
        radius = math.hypot(x, y)
        if not (34.0 < radius < 56.0 and -74.0 < z < -30.0):
            continue
        # Zero displacement at each end; maximum 1.35 mm in the middle.
        t = (z + 74.0) / 44.0
        delta = 1.35 * math.sin(math.pi * t) ** 1.35
        vert.co.x *= (radius + delta) / radius
        vert.co.y *= (radius + delta) / radius
        moved += 1

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    print("PROFILE_VERTICES_MOVED", moved)


def round_bell_face(obj):
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    z_front = min(v.co.z for v in bm.verts)
    front_faces = [
        face for face in bm.faces
        if all(abs(v.co.z - z_front) < 0.002 for v in face.verts)
    ]
    front_edges = {edge for face in front_faces for edge in face.edges}
    result = bmesh.ops.subdivide_edges(
        bm,
        edges=list(front_edges),
        cuts=6,
        use_grid_fill=True,
    )

    front_vertices = [
        vert for vert in bm.verts
        if abs(vert.co.z - z_front) < 0.002
        and 47.5 < math.hypot(vert.co.x, vert.co.y) < 54.5
    ]
    r_inner = min(math.hypot(v.co.x, v.co.y) for v in front_vertices)
    r_outer = max(math.hypot(v.co.x, v.co.y) for v in front_vertices)
    for vert in front_vertices:
        radius = math.hypot(vert.co.x, vert.co.y)
        u = (radius - r_inner) / (r_outer - r_inner)
        # The center of the lip remains at the original overall length.
        # Both edges sweep rearward, replacing the flat shelf with a bullnose.
        setback = 1.35 * 0.5 * (1.0 + math.cos(2.0 * math.pi * u))
        vert.co.z += setback

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    print("ROUNDED_LIP_VERTICES", len(front_vertices))


def inner_radius(z):
    # Ray-measured from the imported mesh away from the external sensor
    # features, then adjusted by the same bell-softening displacement.
    base = 39.6967 + (-z - 42.0) * 0.02712
    t = max(0.0, min(1.0, (z + 74.0) / 44.0))
    delta = 1.35 * math.sin(math.pi * t) ** 1.35
    return base + delta


def make_dimple_cutter():
    cutters = []
    row_z_values = VARIANT["rows"]
    cap_depth = VARIANT["depth"]
    cap_radius = 1.65
    sphere_radius = (cap_radius * cap_radius + cap_depth * cap_depth) / (2.0 * cap_depth)
    for row_index, z in enumerate(row_z_values):
        surface_radius = inner_radius(z)
        center_radius = surface_radius - (sphere_radius - cap_depth)
        target_pitch = 4.15
        count = max(48, round(2.0 * math.pi * surface_radius / target_pitch))
        offset = 0.5 if row_index % 2 else 0.0
        for index in range(count):
            angle = 2.0 * math.pi * (index + offset) / count
            bpy.ops.mesh.primitive_ico_sphere_add(
                subdivisions=3,
                radius=sphere_radius,
                location=(
                    center_radius * math.cos(angle),
                    center_radius * math.sin(angle),
                    z,
                ),
            )
            cutters.append(bpy.context.object)

    bpy.ops.object.select_all(action="DESELECT")
    for cutter in cutters:
        cutter.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    cutter = bpy.context.object
    cutter.name = "Dimple_Cutter"
    print("DIMPLE_COUNT", len(cutters))
    return cutter


def apply_dimples(body, cutter):
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    cutter.select_set(False)
    modifier = body.modifiers.new(name="Shallow recessed dimples", type="BOOLEAN")
    modifier.operation = "DIFFERENCE"
    modifier.solver = "EXACT"
    modifier.object = cutter
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    body.data.validate(verbose=True)
    body.data.update()
    edge_uses = {}
    body.data.calc_loop_triangles()
    for tri in body.data.loop_triangles:
        ids = tri.vertices
        for a, b in ((ids[0], ids[1]), (ids[1], ids[2]), (ids[2], ids[0])):
            key = (min(a, b), max(a, b))
            edge_uses[key] = edge_uses.get(key, 0) + 1
    nonmanifold = sum(1 for count in edge_uses.values() if count != 2)
    print("RESULT_VERTICES", len(body.data.vertices), "RESULT_FACES", len(body.data.polygons))
    print("NONMANIFOLD_EDGES", nonmanifold)


def add_box(name, location, dimensions):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return obj


def add_x_cylinder(name, center, radius, depth, vertices=64):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        location=center,
        rotation=(0.0, math.pi / 2.0, 0.0),
    )
    obj = bpy.context.object
    obj.name = name
    return obj


def boolean_apply(body, tool, operation, label):
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    tool.select_set(False)
    modifier = body.modifiers.new(name=label, type="BOOLEAN")
    modifier.operation = operation
    modifier.solver = "EXACT"
    modifier.object = tool
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.data.objects.remove(tool, do_unlink=True)
    body.data.validate(verbose=True)
    body.data.update()


def add_map_snap_mount(body):
    # Photo-measured holder for Bosch 0 261 230 042 / DS-S2-TF:
    #   overall width 42.76 mm, body height 22.41 mm,
    #   body thickness 6.37 mm, sensing nose 11.88 mm.
    # The flat sensor seats against a tangent backplate. Split lower ledges
    # leave the connector open, while the top cantilever snaps over its face.
    pickup_z = 32.0
    pickup_y = -5.5
    seat_x = 51.5

    positive_parts = [
        # Wide, shallow sealing pad follows the sensor's flat housing.
        add_box(
            "MAP_Sealing_Backplate",
            ((39.0 + seat_x) * 0.5, 0.0, pickup_z),
            (seat_x - 39.0, 49.0, 29.0),
        ),
        # Narrow side guides locate the measured 42.76 mm overall width.
        add_box("MAP_Guide_Left", (56.0, -22.1, pickup_z), (9.0, 2.8, 26.0)),
        add_box("MAP_Guide_Right", (56.0, 22.1, pickup_z), (9.0, 2.8, 26.0)),
        # Two corner ledges carry the sensor while leaving its connector open.
        add_box("MAP_Lower_Ledge_Left", (56.0, -16.5, pickup_z - 12.5), (9.0, 10.0, 3.0)),
        add_box("MAP_Lower_Ledge_Right", (56.0, 16.5, pickup_z - 12.5), (9.0, 10.0, 3.0)),
        # A central cantilever flexes upward as the 6.37 mm body is pushed in.
        add_box("MAP_Tab_Root", (50.0, 0.0, pickup_z + 15.0), (4.0, 10.0, 9.0)),
        add_box("MAP_Tab_Arm", (56.5, 0.0, pickup_z + 18.3), (13.0, 10.0, 2.4)),
        add_box("MAP_Tab_Hook", (62.0, 0.0, pickup_z + 15.6), (2.0, 10.0, 5.5)),
    ]
    for index, part in enumerate(positive_parts):
        boolean_apply(body, part, "UNION", f"MAP mount union {index + 1}")

    # The measured sensing cage is 11.88 mm. A 12.4 mm guide bore accepts it;
    # the larger 14.2 mm mouth gives the green O-ring a lead-in and sealing
    # land. Final sealing interference can be tuned after a short test print.
    o_ring_land = add_x_cylinder(
        "MAP_ORing_Land",
        (49.0, pickup_y, pickup_z),
        7.1,
        8.0,
    )
    boolean_apply(body, o_ring_land, "DIFFERENCE", "MAP O-ring sealing land")
    probe_socket = add_x_cylinder(
        "MAP_Probe_Socket",
        (44.5, pickup_y, pickup_z),
        6.2,
        9.0,
    )
    boolean_apply(body, probe_socket, "DIFFERENCE", "MAP stepped probe socket")
    pressure_gallery = add_x_cylinder(
        "MAP_Pressure_Gallery",
        (38.5, pickup_y, pickup_z),
        1.2,
        12.0,
        vertices=48,
    )
    boolean_apply(body, pressure_gallery, "DIFFERENCE", "MAP pressure gallery")
    print(
        "MAP_MOUNT",
        "PROBE_BORE_MM", 12.4,
        "ORING_LAND_MM", 14.2,
        "GALLERY_MM", 2.4,
        "GUIDE_INSIDE_MM", 42.8,
        "BODY_HEIGHT_MM", 22.4,
        "BODY_THICKNESS_MM", 6.4,
        "PICKUP_Z_MM", pickup_z,
        "PICKUP_Y_MM", pickup_y,
    )


def weld_boolean_roundoff(body):
    # Exact booleans can leave coincident vertices a few ten-thousandths of a
    # millimetre apart in the dense dimple mesh. Welding only within 1 micron
    # removes those export degeneracies without changing printable geometry.
    bm = bmesh.new()
    bm.from_mesh(body.data)
    before = len(bm.verts)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.001)
    bm.normal_update()
    bm.to_mesh(body.data)
    bm.free()
    body.data.validate(verbose=True)
    body.data.update()
    print("BOOLEAN_WELD_VERTICES", before - len(body.data.vertices))


def write_binary_stl(obj, path):
    mesh = obj.data
    mesh.calc_loop_triangles()
    with open(path, "wb") as stream:
        stream.write(b"Codex velocity stack: gradual bell with shallow dimples".ljust(80, b"\0"))
        stream.write(struct.pack("<I", len(mesh.loop_triangles)))
        for tri in mesh.loop_triangles:
            normal = tri.normal.normalized()
            stream.write(struct.pack("<3f", normal.x, normal.y, normal.z))
            for vertex_index in tri.vertices:
                co = obj.matrix_world @ mesh.vertices[vertex_index].co
                stream.write(struct.pack("<3f", co.x, co.y, co.z))
            stream.write(struct.pack("<H", 0))


def replace_object_mesh(model_root, object_id, obj, inverse_transform):
    source_object = model_root.find(f".//m:object[@id='{object_id}']", NS)
    old_mesh = source_object.find("./m:mesh", NS)
    source_object.remove(old_mesh)
    mesh_node = ET.SubElement(source_object, f"{{{CORE_NS}}}mesh")
    vertices_node = ET.SubElement(mesh_node, f"{{{CORE_NS}}}vertices")
    triangles_node = ET.SubElement(mesh_node, f"{{{CORE_NS}}}triangles")

    mesh = obj.data
    for vertex in mesh.vertices:
        source_co = (inverse_transform @ (obj.matrix_world @ vertex.co).to_4d()).to_3d()
        ET.SubElement(vertices_node, f"{{{CORE_NS}}}vertex", {
            "x": f"{source_co.x:.7g}",
            "y": f"{source_co.y:.7g}",
            "z": f"{source_co.z:.7g}",
        })
    mesh.calc_loop_triangles()
    for tri in mesh.loop_triangles:
        ET.SubElement(triangles_node, f"{{{CORE_NS}}}triangle", {
            "v1": str(tri.vertices[0]),
            "v2": str(tri.vertices[1]),
            "v3": str(tri.vertices[2]),
        })


def write_3mf(model_root, body):
    ET.register_namespace("", CORE_NS)
    ET.register_namespace("p", PROD_NS)
    model_bytes = ET.tostring(model_root, encoding="utf-8", xml_declaration=True)
    temp_path = OUTPUT_3MF + ".tmp"
    with zipfile.ZipFile(SOURCE_3MF, "r") as source, zipfile.ZipFile(
        temp_path, "w", compression=zipfile.ZIP_DEFLATED
    ) as destination:
        for item in source.infolist():
            data = model_bytes if item.filename == MODEL_ENTRY else source.read(item.filename)
            destination.writestr(item, data)
    os.replace(temp_path, OUTPUT_3MF)


def render_preview(body):
    material = bpy.data.materials.new("Dark polymer")
    material.diffuse_color = (0.42, 0.48, 0.56, 1.0)
    material.metallic = 0.05
    material.roughness = 0.28
    body.data.materials.append(material)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "WORLD"
    scene.display.shading.curvature_ridge_factor = 1.8
    scene.display.shading.curvature_valley_factor = 1.5
    scene.render.resolution_x = 1000
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world.color = (0.35, 0.35, 0.35)

    bpy.ops.object.light_add(type="AREA", location=(85, -110, -20))
    bpy.context.object.data.energy = 4800
    bpy.context.object.data.size = 115
    bpy.ops.object.light_add(type="AREA", location=(-80, 35, -95))
    bpy.context.object.data.energy = 3200
    bpy.context.object.data.size = 90

    camera_data = bpy.data.cameras.new("PreviewCamera")
    camera = bpy.data.objects.new("PreviewCamera", camera_data)
    bpy.context.collection.objects.link(camera)
    scene.camera = camera
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 122

    target = Vector((0, 0, -55))
    camera.location = Vector((28, -34, -205))
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(OUTPUT_DIR, f"{VARIANT['label']}_preview.png")
    bpy.ops.render.render(write_still=True)
    if VARIANT.get("map_mount", False):
        target = Vector((47, 0, 30))
        camera.location = Vector((155, -125, 82))
        camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
        camera.data.ortho_scale = 105
        scene.render.filepath = os.path.join(
            OUTPUT_DIR, f"{VARIANT['label']}_mount_preview.png"
        )
        bpy.ops.render.render(write_still=True)


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    model_root, components = parse_source()
    vertices, faces = mesh_data(model_root, "1")
    body = create_object("Velocity_Stack_Modified", vertices, faces, components["1"])

    if VARIANT.get("rounded_lip", False):
        round_bell_face(body)
    soften_bell(body)
    if VARIANT["rows"]:
        cutter = make_dimple_cutter()
        apply_dimples(body, cutter)
    else:
        print("DIMPLE_COUNT", 0)
    if VARIANT.get("map_mount", False):
        add_map_snap_mount(body)
        weld_boolean_roundoff(body)

    write_binary_stl(body, OUTPUT_STL)
    replace_object_mesh(model_root, "1", body, components["1"].inverted())
    write_3mf(model_root, body)
    render_preview(body)
    bpy.ops.wm.save_as_mainfile(filepath=OUTPUT_BLEND)
    print("OUTPUT_3MF", OUTPUT_3MF)
    print("OUTPUT_STL", OUTPUT_STL)
    print("OUTPUT_BLEND", OUTPUT_BLEND)


if __name__ == "__main__":
    main()
