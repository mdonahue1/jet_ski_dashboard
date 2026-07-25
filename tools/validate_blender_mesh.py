import bpy
import bmesh
import sys

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if args:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.stl_import(filepath=args[0])

obj = next(obj for obj in bpy.data.objects if obj.type == "MESH")
bm = bmesh.new()
bm.from_mesh(obj.data)
bad_edges = sum(1 for edge in bm.edges if len(edge.link_faces) != 2)
components = 0
remaining = set(bm.verts)
component_bounds = []
while remaining:
    components += 1
    seed = remaining.pop()
    stack = [seed]
    component_vertices = [seed]
    while stack:
        vertex = stack.pop()
        for edge in vertex.link_edges:
            other = edge.other_vert(vertex)
            if other in remaining:
                remaining.remove(other)
                stack.append(other)
                component_vertices.append(other)
    component_bounds.append((
        tuple(round(min(v.co[i] for v in component_vertices), 3) for i in range(3)),
        tuple(round(max(v.co[i] for v in component_vertices), 3) for i in range(3)),
    ))
print(
    "VALIDATION", obj.name, len(bm.verts), len(bm.faces),
    "NONMANIFOLD_EDGES", bad_edges, "COMPONENTS", components,
    "COMPONENT_BOUNDS", component_bounds
)
bm.free()
