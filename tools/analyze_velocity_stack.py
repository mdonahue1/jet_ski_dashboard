import bpy
import math
import os
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from mathutils import Vector


SOURCE_3MF = r"C:\Users\miner\OneDrive\Desktop\main folder\3d prints\velocity stack with flow straightener,maf,iat bung v1.0.3mf"
OUTPUT_DIR = r"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\velocity_stack_work"
NS = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}


def transform_vertex(v, values):
    x, y, z = v
    a, b, c, d, e, f, g, h, i, tx, ty, tz = values
    return (
        x * a + y * d + z * g + tx,
        x * b + y * e + z * h + ty,
        x * c + y * f + z * i + tz,
    )


def parse_mesh(model_root, object_id):
    obj = model_root.find(f".//m:object[@id='{object_id}']", NS)
    vertices = [
        (float(v.attrib["x"]), float(v.attrib["y"]), float(v.attrib["z"]))
        for v in obj.findall("./m:mesh/m:vertices/m:vertex", NS)
    ]
    faces = [
        (int(t.attrib["v1"]), int(t.attrib["v2"]), int(t.attrib["v3"]))
        for t in obj.findall("./m:mesh/m:triangles/m:triangle", NS)
    ]
    return vertices, faces


def add_mesh(name, vertices, faces):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def setup_render(objects):
    all_points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    mins = Vector((min(p.x for p in all_points), min(p.y for p in all_points), min(p.z for p in all_points)))
    maxs = Vector((max(p.x for p in all_points), max(p.y for p in all_points), max(p.z for p in all_points)))
    center = (mins + maxs) * 0.5
    size = max(maxs - mins)

    material = bpy.data.materials.new("VelocityStackMaterial")
    material.diffuse_color = (0.42, 0.48, 0.56, 1.0)
    material.metallic = 0.1
    material.roughness = 0.32
    for obj in objects:
        obj.data.materials.append(material)

    world = bpy.context.scene.world
    world.color = (0.12, 0.12, 0.12)

    bpy.ops.object.light_add(type="AREA", location=center + Vector((size, -size, size)))
    bpy.context.object.data.energy = 5000
    bpy.context.object.data.shape = "DISK"
    bpy.context.object.data.size = size

    camera_data = bpy.data.cameras.new("Camera")
    camera = bpy.data.objects.new("Camera", camera_data)
    bpy.context.collection.objects.link(camera)
    bpy.context.scene.camera = camera
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = size * 1.25

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 900
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"

    views = {
        "front": Vector((0, -2.2, 0.15)),
        "side": Vector((2.2, 0, 0.15)),
        "iso": Vector((1.5, -1.5, 1.15)),
        "bell": Vector((0.25, -0.2, -2.2)),
    }
    for name, direction in views.items():
        camera.location = center + direction.normalized() * size * 2.3
        camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(OUTPUT_DIR, f"{name}.png")
        bpy.ops.render.render(write_still=True)

    print("BOUNDS", tuple(round(x, 4) for x in mins), tuple(round(x, 4) for x in maxs))


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    with zipfile.ZipFile(SOURCE_3MF) as archive:
        root_model = ET.fromstring(archive.read("3D/3dmodel.model"))
        object_model = ET.fromstring(archive.read("3D/Objects/object_1.model"))

    component_parent = root_model.find(".//m:object[@id='3']", NS)
    objects = []
    for index, component in enumerate(component_parent.findall("./m:components/m:component", NS), 1):
        object_id = component.attrib["objectid"]
        transform = tuple(float(x) for x in component.attrib.get(
            "transform", "1 0 0 0 1 0 0 0 1 0 0 0"
        ).split())
        vertices, faces = parse_mesh(object_model, object_id)
        vertices = [transform_vertex(v, transform) for v in vertices]
        obj = add_mesh(f"Component_{object_id}", vertices, faces)
        objects.append(obj)
        print(
            "OBJECT",
            object_id,
            "VERTICES",
            len(vertices),
            "FACES",
            len(faces),
            "MANIFOLD_HINT",
            len(obj.data.edges),
        )
        if object_id == "1":
            rings = {}
            for x, y, z in vertices:
                if -0.35 < math.atan2(y, x) < 0.35 and math.hypot(x, y) > 34:
                    key = round(z, 3)
                    rings.setdefault(key, []).append(math.hypot(x, y))
            for z_key, ring_radii in sorted(rings.items()):
                if len(ring_radii) >= 8:
                    print(
                        "RING",
                        z_key,
                        len(ring_radii),
                        round(min(ring_radii), 3),
                        round(max(ring_radii), 3),
                    )
            for z0 in range(-82, 84, 5):
                radii = sorted(
                    math.hypot(x, y)
                    for x, y, z in vertices
                    if abs(z - z0) < 0.75 and -0.4 < math.atan2(y, x) < 0.4
                )
                if radii:
                    picks = [radii[int((len(radii) - 1) * q)] for q in (0.05, 0.25, 0.5, 0.75, 0.95)]
                    print("PROFILE", z0, len(radii), *(round(v, 3) for v in picks))

    setup_render(objects)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUTPUT_DIR, "source_import.blend"))


if __name__ == "__main__":
    main()
