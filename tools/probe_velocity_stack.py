import bpy
from mathutils import Vector

obj = bpy.data.objects.get("Component_1") or bpy.data.objects.get("Velocity_Stack_Modified")
for z in range(-80, -27, 2):
    hit, location, normal, face_index = obj.ray_cast(
        Vector((0.0, 0.0, float(z))), Vector((1.0, 0.0, 0.0))
    )
    print("RAY", z, hit, round(location.x, 4) if hit else None, face_index)
