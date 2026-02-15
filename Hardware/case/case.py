import bpy
import bmesh
import math

def create_mgts_model():
# --- 0. シーンのクリーンアップ ---
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
bpy.ops.object.mode_set(mode='OBJECT')
for obj in bpy.data.objects:
bpy.data.objects.remove(obj, do_unlink=True)
for mesh in bpy.data.meshes:
bpy.data.meshes.remove(mesh)
for mat in bpy.data.materials:
bpy.data.materials.remove(mat)

# --- 1. 寸法設定 (単位: cm) ---
total_width = 65.0
case_height = 9.0
case_depth  = 6.5
wall_thickness = 0.3

# LEDパネル・アクリル固定用設定
acrylic_depth = 0.32       # アクリル板の厚み
stopper_offset_from_front = 0.52 # 前面から突起までの距離 (アクリル + 遊び)

# 突起（ストッパー）自体のサイズ
stopper_size_x = 1.0
stopper_size_y = 0.4
stopper_size_z = 0.3
num_stoppers_per_edge = 2 # 各セグメントの上下に配置する突起の数

# 3分割設定
num_segments = 3
segment_width = total_width / num_segments
gap = 0.05

# ★土台（ステー）固定位置の設定
# mount_offset_x = 20.0 の場合、中心から左右に20cmずつで「穴の間隔はちょうど 40cm」になります。
# ステーの長さに余裕を持たせたい場合は 19.0 (38cm間隔) などに調整してください。
mount_block_height = 0.8
mount_block_width = 4.0
mount_block_depth = case_depth
mount_offset_x = 16.0

def get_mat(name, color):
m = bpy.data.materials.new(name=name)
m.use_nodes = True
m.node_tree.nodes["Principled BSDF"].inputs[0].default_value = color
return m

# --- 2. 筐体生成 ---
case_parts = []
for i in range(num_segments):
name = ["Left", "Center", "Right"][i]
x_pos = -total_width/2 + (segment_width/2) + (i * segment_width)

# A. 外殻の作成
bpy.ops.mesh.primitive_cube_add(size=1, location=(x_pos, 0, 0))
obj = bpy.context.active_object
obj.name = f"Segment_{name}"
obj.scale = (segment_width - gap, case_depth, case_height)
bpy.ops.object.transform_apply(scale=True)
obj.data.materials.append(get_mat(f"Mat_{name}", (0.02, 0.02, 0.02, 1)))
case_parts.append(obj)

# B. 底面の補強ブロック (LeftとRightのみ)
if i == 0 or i == 2:
bx = -mount_offset_x if i == 0 else mount_offset_x
bpy.ops.mesh.primitive_cube_add(size=1, location=(bx, 0, -case_height/2 - mount_block_height/2))
block = bpy.context.active_object
block.scale = (mount_block_width, mount_block_depth, mount_block_height)
bpy.ops.object.transform_apply(scale=True)
mod_union = obj.modifiers.new(name="MountBlock", type='BOOLEAN')
mod_union.object = block; mod_union.operation = 'UNION'
bpy.context.view_layer.objects.active = obj; bpy.ops.object.modifier_apply(modifier="MountBlock")
bpy.data.objects.remove(block, do_unlink=True)

# C. 内部くり抜き (背面と端の壁を保護)
h_width = segment_width + 0.2
h_x_off = 0
if i == 0:
h_width = segment_width - wall_thickness + 0.1
h_x_off = wall_thickness / 2
elif i == 2:
h_width = segment_width - wall_thickness + 0.1
h_x_off = -wall_thickness / 2

# 内部を大きくくり抜く（背面の壁3mmだけ残す）
h_depth = case_depth - (wall_thickness * 2)
bpy.ops.mesh.primitive_cube_add(size=1, location=(x_pos + h_x_off, 0, 0))
hollow = bpy.context.active_object
hollow.scale = (h_width, h_depth, case_height - wall_thickness * 2)
bpy.ops.object.transform_apply(scale=True)
mod = obj.modifiers.new(name="Hollow", type='BOOLEAN')
mod.object = hollow; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = obj; bpy.ops.object.modifier_apply(modifier="Hollow")
bpy.data.objects.remove(hollow, do_unlink=True)

# D. アクリル板用段差 (一番手前を削る)
recess_y = -case_depth/2 + acrylic_depth/2
bpy.ops.mesh.primitive_cube_add(size=1, location=(x_pos + h_x_off, recess_y, 0))
recess = bpy.context.active_object
recess.scale = (h_width, acrylic_depth + 0.01, case_height - wall_thickness * 2 + 0.4)
bpy.ops.object.transform_apply(scale=True)
mod = obj.modifiers.new(name="Recess", type='BOOLEAN')
mod.object = recess; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = obj; bpy.ops.object.modifier_apply(modifier="Recess")
bpy.data.objects.remove(recess, do_unlink=True)

# E. ★突起状ストッパーの追加 (UNION)
sy = -case_depth / 2 + stopper_offset_from_front
iz_inner_top = (case_height / 2) - wall_thickness
x_min = x_pos - (segment_width / 2) + 2.0
x_max = x_pos + (segment_width / 2) - 2.0

for side in [1, -1]: # 上側と下側
for j in range(num_stoppers_per_edge):
sx = x_min + (j * (x_max - x_min) / (num_stoppers_per_edge - 1)) if num_stoppers_per_edge > 1 else x_pos
sz = (iz_inner_top - (stopper_size_z / 2)) * side

bpy.ops.mesh.primitive_cube_add(size=1, location=(sx, sy, sz))
stop_node = bpy.context.active_object
stop_node.scale = (stopper_size_x, stopper_size_y, stopper_size_z)
bpy.ops.object.transform_apply(scale=True)

mod_u = obj.modifiers.new(name="Stopper", type='BOOLEAN')
mod_u.object = stop_node; mod_u.operation = 'UNION'
bpy.context.view_layer.objects.active = obj; bpy.ops.object.modifier_apply(modifier="Stopper")
bpy.data.objects.remove(stop_node, do_unlink=True)

# --- 3. 固定穴の作成 (補強ブロック部分) ---
for i in [0, 2]:
seg = case_parts[i]
bx = -mount_offset_x if i == 0 else mount_offset_x
bz = -case_height/2 - mount_block_height
# ネジ貫通穴
bpy.ops.mesh.primitive_cylinder_add(radius=0.33, depth=3.0, location=(bx, 0, bz))
hole = bpy.context.active_object
mod = seg.modifiers.new(name="ScrewHole", type='BOOLEAN'); mod.object = hole; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = seg; bpy.ops.object.modifier_apply(modifier="ScrewHole")
bpy.data.objects.remove(hole, do_unlink=True)
# ナットポケット
bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.64, depth=0.6, location=(bx, 0, bz + 0.6))
nut_p = bpy.context.active_object; nut_p.rotation_euler[2] = math.radians(30)
mod = seg.modifiers.new(name="NutPocket", type='BOOLEAN'); mod.object = nut_p; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = seg; bpy.ops.object.modifier_apply(modifier="NutPocket")
bpy.data.objects.remove(nut_p, do_unlink=True)

# --- 4. ポート類の穴あけ (背面) ---
left_seg = case_parts[0]
y_back_wall = case_depth / 2

# LANポート
bpy.ops.mesh.primitive_cube_add(size=1, location=(-30.0, y_back_wall, -1.0))
lan_h = bpy.context.active_object; lan_h.scale = (1.6, wall_thickness * 4, 1.6); bpy.ops.object.transform_apply(scale=True)
mod = left_seg.modifiers.new(name="LAN", type='BOOLEAN'); mod.object = lan_h; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = left_seg; bpy.ops.object.modifier_apply(modifier="LAN")
bpy.data.objects.remove(lan_h, do_unlink=True)

# DCジャック
bpy.ops.mesh.primitive_cylinder_add(radius=0.6, depth=wall_thickness * 4, location=(-27.0, y_back_wall, -1.0))
dc_h = bpy.context.active_object; dc_h.rotation_euler[0] = math.radians(90)
mod = left_seg.modifiers.new(name="DC", type='BOOLEAN'); mod.object = dc_h; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = left_seg; bpy.ops.object.modifier_apply(modifier="DC")
bpy.data.objects.remove(dc_h, do_unlink=True)

# 操作ボタン (Right天面)
right_seg = case_parts[2]
bpy.ops.mesh.primitive_cylinder_add(radius=1.5, depth=2.0, location=(28, 0, case_height/2))
btn_c = bpy.context.active_object
mod = right_seg.modifiers.new(name="BTN", type='BOOLEAN'); mod.object = btn_c; mod.operation = 'DIFFERENCE'
bpy.context.view_layer.objects.active = right_seg; bpy.ops.object.modifier_apply(modifier="BTN")
bpy.data.objects.remove(btn_c, do_unlink=True)

print(f"Model Ready: Mounting hole distance is {mount_offset_x * 2} cm.")

if __name__ == "__main__":
create_mgts_model()
