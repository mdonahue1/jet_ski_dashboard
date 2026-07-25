import bpy
import os
from mathutils import Vector


SOURCE = r"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\dash_housing_work\source_geometry.stl"
OUTPUT = r"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\dash_housing_work"

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.stl_import(filepath=SOURCE)
body = bpy.context.object
body.name = "Existing_Dashboard_Housing"

material = bpy.data.materials.new("Housing")
material.diffuse_color = (0.15, 0.22, 0.30, 1.0)
body.data.materials.append(material)

points = [body.matrix_world @ Vector(corner) for corner in body.bound_box]
mins = Vector(tuple(min(p[i] for p in points) for i in range(3)))
maxs = Vector(tuple(max(p[i] for p in points) for i in range(3)))
center = (mins + maxs) * 0.5
extent = max(maxs - mins)

camera_data = bpy.data.cameras.new("Camera")
camera = bpy.data.objects.new("Camera", camera_data)
bpy.context.collection.objects.link(camera)
camera.data.type = "ORTHO"
camera.data.ortho_scale = extent * 1.2
bpy.context.scene.camera = camera

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x = 1000
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "WORLD"
scene.display.shading.curvature_ridge_factor = 1.6
scene.display.shading.curvature_valley_factor = 1.4

views = {
    "existing_iso": Vector((1.4, -1.5, 1.2)),
    "existing_front": Vector((0.0, -1.0, 0.0)),
    "existing_top": Vector((0.0, 0.0, 1.0)),
    "existing_side": Vector((1.0, 0.0, 0.0)),
}
for name, direction in views.items():
    camera.location = center + direction.normalized() * extent * 2.5
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(OUTPUT, name + ".png")
    bpy.ops.render.render(write_still=True)

print("BOUNDS_MM", tuple(round(v, 3) for v in mins), tuple(round(v, 3) for v in maxs))
