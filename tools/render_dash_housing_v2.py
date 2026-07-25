import bpy
import os
from mathutils import Vector


ROOT = r"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\dash_housing_work\revision_2"
TRAY = os.path.join(ROOT, "jet_ski_dash_tray_v2.STL")
COVER = os.path.join(ROOT, "jet_ski_dash_cover_v2.STL")

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)

def load(path, name, color):
    bpy.ops.wm.stl_import(filepath=path)
    obj = bpy.context.object
    obj.name = name
    material = bpy.data.materials.new(name + "_Material")
    material.diffuse_color = color
    obj.data.materials.append(material)
    return obj

tray = load(TRAY, "Revision_2_Tray", (0.08, 0.16, 0.22, 1.0))
cover = load(COVER, "Revision_2_Cover", (0.18, 0.25, 0.31, 1.0))
objects = [tray, cover]

points = [
    obj.matrix_world @ Vector(corner)
    for obj in objects
    for corner in obj.bound_box
]
mins = Vector(tuple(min(p[i] for p in points) for i in range(3)))
maxs = Vector(tuple(max(p[i] for p in points) for i in range(3)))
center = (mins + maxs) * 0.5
extent = max(maxs - mins)

camera_data = bpy.data.cameras.new("Camera")
camera = bpy.data.objects.new("Camera", camera_data)
bpy.context.collection.objects.link(camera)
camera.data.type = "ORTHO"
camera.data.ortho_scale = extent * 1.28
bpy.context.scene.camera = camera

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x = 1200
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "WORLD"

views = {
    "dash_v2_assembled_iso": Vector((1.35, -1.5, 1.0)),
    "dash_v2_rider_side": Vector((0.0, -1.0, 0.25)),
    "dash_v2_side": Vector((1.0, 0.0, 0.15)),
}
for name, direction in views.items():
    camera.location = center + direction.normalized() * extent * 2.7
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(ROOT, name + ".png")
    bpy.ops.render.render(write_still=True)

# Exploded view: lift the removable cover without changing the tray.
cover.location.z += extent * 0.45
points = [
    obj.matrix_world @ Vector(corner)
    for obj in objects
    for corner in obj.bound_box
]
center_exploded = sum(points, Vector()) / len(points)
camera.location = center_exploded + Vector((1.35, -1.5, 1.0)).normalized() * extent * 3.0
camera.rotation_euler = (center_exploded - camera.location).to_track_quat("-Z", "Y").to_euler()
camera.data.ortho_scale = extent * 1.55
scene.render.filepath = os.path.join(ROOT, "dash_v2_exploded.png")
bpy.ops.render.render(write_still=True)

print("BOUNDS_MM", tuple(round(v, 3) for v in mins), tuple(round(v, 3) for v in maxs))
