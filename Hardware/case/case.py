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
    # LEDパネルが前面から飛び出していたため、突起位置を3mm奥へ移動(0.52→0.82cm)
    stopper_offset_from_front = 0.82 # 前面から突起までの距離 (アクリル + 遊び)

    # 突起（ストッパー）自体のサイズ
    # 分割箇所(セグメントの継ぎ目)付近でパネルを支えられるよう、
    # サイズを拡大(高さ・左右幅とも)し、位置も継ぎ目寄りに変更。
    stopper_size_x = 1.8   # 左右幅: 1.0 → 1.8cm
    stopper_size_y = 0.4
    stopper_size_z = 0.5   # 高さ: 0.3 → 0.5cm
    # 隙間が気になるとのことなので、余裕を持たせず「突起の外側端が
    # セグメントの外壁端にぴったり揃う(面一)」位置を式で直接計算します
    # (下のループ内で各セグメントの実幅 seg_scale_x から算出)。
    num_stoppers_per_edge = 2 # 各セグメントの上下に配置する突起の数

    # アクリル用の溝(段差)も、突起と同様に前面から3mm奥へ移動
    groove_extra_offset = 0.3  # cm (3mm)

    # 横の開口が狭くアクリル板を削って入れていたとのことなので、片側2.5mmの
    # 遊びを確保したい。溝だけを広げて端の壁を削る(薄くする)のではなく、
    # 「単純に左右のケースの長さを延ばす」方針に変更(ユーザー提案)。
    # Left/Rightの継ぎ目側(センターと接する側)の位置は変えず、外側の端だけを
    # 1.5mm延長します。壁の厚み(2.25mm)はそのまま保たれ、溝も内部くり抜きも
    # セグメント自体の拡大に合わせて自動的に外側へ広がる形になります。
    end_extension = 0.15  # cm (1.5mm、Left/Right外側の端のみ延長)

    # 3分割設定
    num_segments = 3
    segment_width = total_width / num_segments
    gap = 0.05
    center_extra_width_each_side = 0.05  # センターの左右それぞれ+0.5mm(0.05cm)拡張。
    # 元の隙間(gap=0.5mm)をちょうど埋めて面一にする値。片側+1mmだと0.5mm重なって
    # Left/Rightと干渉するため、重ならない適切な値(0.5mm/側)に戻しています。

    # ★土台（ステー）固定位置の設定
    # カメラ固定プレート(1/4"&3/8"ネジ)2個を底面側の固定に使用する想定。
    # プレート側で固定が完結するため、ケース側のネジ穴・ナットポケットは廃止。
    # 土台はケース両端にフラッシュ（面一）で揃うよう、位置を式から自動計算しています。
    mount_block_height = 1.0   # 高さ 8mm → 10mm
    mount_block_width = 2.4    # 幅 40mm → 24mm
    mount_block_depth = case_depth
    case_outer_x = total_width / 2 - gap / 2 + end_extension  # 両端セグメントの外側の端(絶対値、延長後)
    mount_offset_x = case_outer_x - mount_block_width / 2  # 土台の外側端をケース端に合わせる(面一)

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
        # センターだけ左右それぞれ0.5mm(=0.05cm)ずつ幅を拡張。
        # 組み付け時に実測でセンター側に隙間が出ていたための調整。
        seg_scale_x = segment_width - gap
        if i == 1:
            seg_scale_x += center_extra_width_each_side * 2
        elif i == 0 or i == 2:
            # Left/Rightは外側の端だけを1.5mm延長(継ぎ目側の位置は不変)。
            # 幅を+end_extensionし、中心位置を外側へ半分ずらすことで、
            # 継ぎ目側の境界(center_seg側)はそのまま、外側の端だけが延びる。
            seg_scale_x += end_extension
            x_pos += -end_extension / 2 if i == 0 else end_extension / 2
        bpy.ops.mesh.primitive_cube_add(size=1, location=(x_pos, 0, 0))
        obj = bpy.context.active_object
        obj.name = f"Segment_{name}"
        obj.scale = (seg_scale_x, case_depth, case_height)
        bpy.ops.object.transform_apply(scale=True)
        obj.data.materials.append(get_mat(f"Mat_{name}", (0.02, 0.02, 0.02, 1)))
        case_parts.append(obj)

        # B. 底面の補強ブロック (LeftとRightの外側の端のみ。継ぎ目側はブロックでは
        # なくM3穴で対応するため、下の「継ぎ目のM3穴」セクションで別途あけます)
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
            # 外側の端が end_extension 分延長された分だけ、くり抜きの幅も
            # 同じだけ広げる(壁の厚みは wall_thickness/2 のオフセットのまま
            # 変わらないので、端の肉厚2.25mmは保たれる)。
            h_width = segment_width - wall_thickness + 0.1 + end_extension
            h_x_off = wall_thickness / 2
        elif i == 2:
            h_width = segment_width - wall_thickness + 0.1 + end_extension
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
        # 前回、溝の「位置」だけをgroove_extra_offset分奥へずらしてしまい、
        # 前面から溝までがつながらず塞がってしまっていた(蓋がされた状態)ため修正。
        # 正しくは「前面から」溝底までの深さ(groove_reach)をgroove_extra_offset分
        # 深くする＝前面の開口は維持したまま、溝そのものを奥へ延長する。
        clean_cut = 0.01  # 前面をクリーンカットするための微小オーバーカット
        groove_reach = acrylic_depth + clean_cut + groove_extra_offset  # 前面から溝底までの深さ

        # 横方向の開口：溝(この段差)は内部くり抜き(C.)と同じ幅・位置を使用。
        # Left/Rightは外側の端自体がすでにend_extension分延長されているため、
        # 溝も自動的に同じだけ外側へ広がる(継ぎ目側の境界は変更なし)。
        recess_y = -case_depth/2 - clean_cut/2 + groove_reach/2  # 前面を含みつつ奥へ延長した中心位置
        bpy.ops.mesh.primitive_cube_add(size=1, location=(x_pos + h_x_off, recess_y, 0))
        recess = bpy.context.active_object
        recess.scale = (h_width, groove_reach + clean_cut, case_height - wall_thickness * 2 + 0.4)
        bpy.ops.object.transform_apply(scale=True)
        mod = obj.modifiers.new(name="Recess", type='BOOLEAN')
        mod.object = recess; mod.operation = 'DIFFERENCE'
        bpy.context.view_layer.objects.active = obj; bpy.ops.object.modifier_apply(modifier="Recess")
        bpy.data.objects.remove(recess, do_unlink=True)

        # E. ★突起状ストッパーの追加 (UNION)
        sy = -case_depth / 2 + stopper_offset_from_front
        iz_inner_top = (case_height / 2) - wall_thickness
        # 突起の外側端をセグメントの実際の外壁端(seg_scale_x/2)にぴったり
        # 揃える(隙間ゼロ)。センターのみ幅が異なるため seg_scale_x を使用。
        x_min = x_pos - (seg_scale_x / 2) + (stopper_size_x / 2)
        x_max = x_pos + (seg_scale_x / 2) - (stopper_size_x / 2)

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

    # --- 3. 固定穴の作成 ---
    # 土台側のネジ穴・ナットポケットは廃止（カメラ固定プレート側で固定が完結するため）。

    # ★天面：エーモンステー(S745相当, 15×397mm, 穴ピッチ2.5cm)用のM6穴
    # ステーを2本、平行に(奥行方向にオフセットして)配置する想定。
    # センターケースの中心(X=0)を基準に2.5cmピッチでグリッドを取り、
    # 左右セグメントはそのグリッド上でできるだけ各セグメント中心に近い点を採用。
    # (中心からの距離 17.5cm = 2.5cm×7ピッチ。ステー全長397mmに対し、
    #  穴間隔35cmなら両端に約2.35cmずつ余裕が残る計算です)
    top_screw_dia = 0.65        # M6クリアランス穴 ⌀6.5mm
    top_screw_depth = 1.0       # 天板厚み0.3cm + clean-cut用の余裕
    top_hole_x = {"Left": -17.5, "Center": 0.0, "Right": 17.5}  # cm、中心からの距離
    top_stay_rows_y = [-1.0, 1.0]  # cm、2本のステーの奥行方向オフセット(平行配置)

    for i, seg_name in enumerate(["Left", "Center", "Right"]):
        seg = case_parts[i]
        hx = top_hole_x[seg_name]
        for ry in top_stay_rows_y:
            bpy.ops.mesh.primitive_cylinder_add(radius=top_screw_dia/2, depth=top_screw_depth,
                                                 location=(hx, ry, case_height/2))
            th = bpy.context.active_object
            mod = seg.modifiers.new(name="TopStayHole", type='BOOLEAN')
            mod.object = th; mod.operation = 'DIFFERENCE'
            bpy.context.view_layer.objects.active = seg; bpy.ops.object.modifier_apply(modifier="TopStayHole")
            bpy.data.objects.remove(th, do_unlink=True)

    # --- 4. ポート類の穴あけ (背面) ---
    left_seg = case_parts[0]
    y_back_wall = case_depth / 2

    # LANポートは廃止（穴なし）
    # 電源ポート(DCジャック)も廃止（穴なし）

    # 操作ボタン (Right天面)
    right_seg = case_parts[2]
    bpy.ops.mesh.primitive_cylinder_add(radius=1.5, depth=2.0, location=(28, 0, case_height/2))
    btn_c = bpy.context.active_object
    mod = right_seg.modifiers.new(name="BTN", type='BOOLEAN'); mod.object = btn_c; mod.operation = 'DIFFERENCE'
    bpy.context.view_layer.objects.active = right_seg; bpy.ops.object.modifier_apply(modifier="BTN")
    bpy.data.objects.remove(btn_c, do_unlink=True)

    # ★WiFiアンテナ用ピグテールケーブル(IPX/U.FL→RP-SMAメス バルクヘッド)の穴
    # 「側面」= 背面(Y+側)と解釈しています。左右は隣接セグメントと接する内部の
    # 継ぎ目(隙間0.5mm)のため外部への引き出しには使えず、前面はLEDパネル、
    # 上面はステー穴とボタンで使用済みのため、背面が唯一の実用的な取付面と
    # 判断しました。もし側面＝別の面を意図されている場合は教えてください。
    # 基盤をファンの風が当たる位置の側面(=ファン付近の上寄り右側)に配置するとの
    # ことなので、穴の位置を中央から右へ6.0cm・上へ2.5cm移動しました。
    antenna_hole_dia = 0.6      # ⌀6mm
    antenna_hole_depth = wall_thickness + 0.4  # 背面の壁(3mm厚)を貫通させる
    antenna_hole_x = 6.0         # センターセグメント中央から右へ6.0cm
    antenna_hole_z = 2.5         # 中央から上へ2.5cm
    center_seg = case_parts[1]
    bpy.ops.mesh.primitive_cylinder_add(radius=antenna_hole_dia/2, depth=antenna_hole_depth,
                                         location=(antenna_hole_x, y_back_wall, antenna_hole_z),
                                         rotation=(math.radians(90), 0, 0))
    ant_hole = bpy.context.active_object
    mod = center_seg.modifiers.new(name="AntennaHole", type='BOOLEAN')
    mod.object = ant_hole; mod.operation = 'DIFFERENCE'
    bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="AntennaHole")
    bpy.data.objects.remove(ant_hole, do_unlink=True)

    # ★USB Type-C 中継ポート(SinLoon製 パネルマウント延長ケーブル、オス-メス)用の穴
    # 商品ページに「ネジ穴径 約3mm」の記載はあるが、コネクタ本体の開口サイズや
    # ブラケット寸法は非公開のため、この種の一般的なパネルマウントUSB-Cブラケット
    # (PCケース用CYブラケット等)の標準的な形状を仮定して設計しています。
    # ・ポート開口: 角穴 10.5mm×4.0mm(USB-Cコネクタ本体+クリアランス)
    # ・左右の固定ネジ穴: ピッチ20mm、⌀3.2mm(商品記載の「約3mm」に対応)
    # アンテナ穴の真下に配置したいとのことなので、同じX位置で下へずらしました。
    # 実物が届いたら実測して寸法を合わせ直してください。
    usb_hole_x = antenna_hole_x  # アンテナ穴と同じX位置(右へ6.0cm)に揃える
    usb_hole_z = 1.2             # アンテナ穴(Z=2.5)の下、間隔0.8cmを確保
    usb_port_width = 1.05        # 角穴 10.5mm(仮)
    usb_port_height = 0.4        # 角穴 4.0mm(仮)
    usb_screw_pitch = 2.0        # 固定ネジ穴ピッチ 20mm(仮)
    usb_screw_dia = 0.32         # ⌀3.2mm(商品記載「約3mm」に対応)
    usb_hole_depth = wall_thickness + 0.4  # 背面の壁(3mm厚)を貫通させる

    # ポート開口(角穴)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(usb_hole_x, y_back_wall, usb_hole_z))
    usb_port = bpy.context.active_object
    usb_port.scale = (usb_port_width, usb_hole_depth, usb_port_height)
    bpy.ops.object.transform_apply(scale=True)
    mod = center_seg.modifiers.new(name="UsbPort", type='BOOLEAN')
    mod.object = usb_port; mod.operation = 'DIFFERENCE'
    bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="UsbPort")
    bpy.data.objects.remove(usb_port, do_unlink=True)

    # 左右の固定ネジ穴
    for sx in (usb_hole_x - usb_screw_pitch / 2, usb_hole_x + usb_screw_pitch / 2):
        bpy.ops.mesh.primitive_cylinder_add(radius=usb_screw_dia/2, depth=usb_hole_depth,
                                             location=(sx, y_back_wall, usb_hole_z),
                                             rotation=(math.radians(90), 0, 0))
        usb_screw = bpy.context.active_object
        mod = center_seg.modifiers.new(name="UsbScrew", type='BOOLEAN')
        mod.object = usb_screw; mod.operation = 'DIFFERENCE'
        bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="UsbScrew")
        bpy.data.objects.remove(usb_screw, do_unlink=True)

    # ★底面：センターセグメントへの5cm冷却ファン取付穴＋通気口
    # ファンは50x50x10(12)mmの汎用5010サイズ。取付穴ピッチ(ねじ位置)は商品情報に
    # 記載がなかったため、50mmファンで一般的な40mm角配置(中心から±20mm)を
    # 仮定しています。実測のピッチが異なる場合は教えてください。
    fan_hole_pitch = 4.0        # 取付穴ピッチ 40mm (仮定・要確認)
    fan_screw_dia = 0.32        # M3クリアランス穴 ⌀3.2mm (仮定)
    fan_vent_dia = 4.0          # 通気口 ⌀40mm。取付穴の対角(約28.3mm)より内側に
                                 # 収め、四隅のねじ止め部を確実に残しています。
    fan_hole_depth = wall_thickness + 0.4  # 底面の壁(3mm厚)を貫通させる

    bpy.ops.mesh.primitive_cylinder_add(radius=fan_vent_dia/2, depth=fan_hole_depth,
                                         location=(0, 0, -case_height/2))
    vent = bpy.context.active_object
    mod = center_seg.modifiers.new(name="FanVent", type='BOOLEAN')
    mod.object = vent; mod.operation = 'DIFFERENCE'
    bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="FanVent")
    bpy.data.objects.remove(vent, do_unlink=True)

    for fx in (-fan_hole_pitch / 2, fan_hole_pitch / 2):
        for fy in (-fan_hole_pitch / 2, fan_hole_pitch / 2):
            bpy.ops.mesh.primitive_cylinder_add(radius=fan_screw_dia/2, depth=fan_hole_depth,
                                                 location=(fx, fy, -case_height/2))
            fh = bpy.context.active_object
            mod = center_seg.modifiers.new(name="FanScrew", type='BOOLEAN')
            mod.object = fh; mod.operation = 'DIFFERENCE'
            bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="FanScrew")
            bpy.data.objects.remove(fh, do_unlink=True)

    # ★底面：書き込み用USBポート(背面のものと同様にSinLoon製パネルマウント
    # 延長ケーブルを想定)の穴。目立たない場所とのことなので、ファン(中心付近)
    # から離した底面の隅(後方寄り・右寄り)に配置しました。寸法は背面のUSB-C
    # ポートと同じ仮定値を使っています。実物のサイズが分かれば合わせて調整します。
    # (継ぎ目に追加したカメラ固定プレート用ブロックと干渉しないよう、
    #  ファンとブロックの間のすき間に位置をずらしています)
    write_usb_x = 4.5             # ファン・継ぎ目ブロックの両方から離した位置(仮)
    write_usb_y = 2.0             # 後方寄り(仮)
    write_usb_port_width = 1.05   # 角穴 10.5mm(仮)
    write_usb_port_height = 0.4   # 角穴 4.0mm(仮)
    write_usb_screw_pitch = 2.0   # 固定ネジ穴ピッチ 20mm(仮)
    write_usb_screw_dia = 0.32    # ⌀3.2mm(仮)
    write_usb_hole_depth = wall_thickness + 0.4  # 底面の壁(3mm厚)を貫通させる

    bpy.ops.mesh.primitive_cube_add(size=1, location=(write_usb_x, write_usb_y, -case_height/2))
    write_usb_port = bpy.context.active_object
    write_usb_port.scale = (write_usb_port_width, write_usb_port_height, write_usb_hole_depth)
    bpy.ops.object.transform_apply(scale=True)
    mod = center_seg.modifiers.new(name="WriteUsbPort", type='BOOLEAN')
    mod.object = write_usb_port; mod.operation = 'DIFFERENCE'
    bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="WriteUsbPort")
    bpy.data.objects.remove(write_usb_port, do_unlink=True)

    for sx in (write_usb_x - write_usb_screw_pitch / 2, write_usb_x + write_usb_screw_pitch / 2):
        bpy.ops.mesh.primitive_cylinder_add(radius=write_usb_screw_dia/2, depth=write_usb_hole_depth,
                                             location=(sx, write_usb_y, -case_height/2))
        write_usb_screw = bpy.context.active_object
        mod = center_seg.modifiers.new(name="WriteUsbScrew", type='BOOLEAN')
        mod.object = write_usb_screw; mod.operation = 'DIFFERENCE'
        bpy.context.view_layer.objects.active = center_seg; bpy.ops.object.modifier_apply(modifier="WriteUsbScrew")
        bpy.data.objects.remove(write_usb_screw, do_unlink=True)

    # ★底面の継ぎ目：カメラ固定プレート(D40-BYQB, 1/4"&3/8"ネジ)用のM3ねじ穴。
    # Center側だけでなく、継ぎ目をまたいで隣接するLeft/Right側にも半分ずつ
    # 穴があくようにしました(プレートが継ぎ目を橋渡しする形)。2cm角の正方形
    # 配置(4箇所)×2継ぎ目=計8箇所。センター拡張により継ぎ目はほぼ面一(隙間ゼロ)
    # なので、その境界位置を中心に穴を振り分けています。
    joint_screw_dia = 0.32       # M3クリアランス穴 ⌀3.2mm
    joint_hole_pitch = 2.0       # 2cm角の正方形配置
    joint_hole_depth = wall_thickness + 0.4  # 底面の壁(3mm厚)を貫通させる
    center_seg_scale_x = segment_width - gap + center_extra_width_each_side * 2
    joint_boundary_x = center_seg_scale_x / 2  # Left/Center・Center/Rightの継ぎ目位置(絶対値)

    # (継ぎ目のX位置, 負側の穴を切るセグメント, 正側の穴を切るセグメント)
    joints = [
        (-joint_boundary_x, left_seg, center_seg),   # Left-Center継ぎ目
        (joint_boundary_x, center_seg, right_seg),   # Center-Right継ぎ目
    ]
    for boundary_x, seg_neg, seg_pos in joints:
        for dx in (-joint_hole_pitch / 2, joint_hole_pitch / 2):
            sx = boundary_x + dx
            target_seg = seg_neg if dx < 0 else seg_pos
            for sy in (-joint_hole_pitch / 2, joint_hole_pitch / 2):
                bpy.ops.mesh.primitive_cylinder_add(radius=joint_screw_dia/2, depth=joint_hole_depth,
                                                     location=(sx, sy, -case_height/2))
                jh = bpy.context.active_object
                mod = target_seg.modifiers.new(name="JointScrew", type='BOOLEAN')
                mod.object = jh; mod.operation = 'DIFFERENCE'
                bpy.context.view_layer.objects.active = target_seg; bpy.ops.object.modifier_apply(modifier="JointScrew")
                bpy.data.objects.remove(jh, do_unlink=True)

    # ★左右の一番外側の側面(端面)：LEDパネル固定用プッシュピン穴
    # (バイクカウル用、中央を押すと抜けるタイプ)
    # 前面はアクリル溝がセグメント幅のほぼ全域を占めていて肉厚がほぼ無いため、
    # 「側面(端面)」に穴を開ける方針に変更。ただしその端面も内部くり抜きの
    # 都合で実測2.25mm厚しかなく、穴まわりの強度に不安があったため、
    # 側面を外側へ肉厚を追加(ローカルな補強ボス)してから穴をあけています。
    # 補強ボスは内部の空洞側へは食い込ませず(アクリル溝を再び塞がないよう)、
    # 既存の肉厚(2.25mm)の外側に追加して、その部分だけ厚みを確保しています。
    # ★エーモン L型ステー(黒, 穴径7mm, 45×35×35mm, S731)をLEDパネル裏側に
    # 両面テープで貼り付け、側面からこの穴へブッシュピンを挿して固定する構成
    # とのことなので、穴径をステーの穴径(⌀7mm)に合わせました。
    # ブッシュピンは4本(左右各2本、上下に振り分け)使う想定に変更。
    # 前後(Y)位置は前面フラッシュではなく、溝の底(奥)より少し奥に配置してい
    # ます(前面寄りだと溝の開口とほぼ同じ場所になり穴周りの肉が薄くなるため)。
    # ステーの実物の穴位置が確認できたら、push_pin_yを置き換えてください。
    push_pin_dia = 0.7              # ⌀7mm (エーモンS731ステーの穴径に合わせ)
    pin_wall_inner = wall_thickness - 0.05 - gap / 2  # 既存の端面の肉厚(実測2.25mm)
    pin_boss_extra = 0.2            # 外側へ追加する肉厚 2mm
    pin_boss_total = pin_wall_inner + pin_boss_extra  # 補強後の合計厚み(4.25mm)
    pin_boss_size_y = 1.4           # 奥行き1.4cm分(⌀7mm穴に対し前後マージン確保)
    pin_boss_size_z = 1.8           # 穴まわりに十分な余裕を持たせた高さ
    push_pin_y = -1.85               # 溝の底(約-2.62cm)より奥、干渉しない位置(暫定・要確認)
    push_pin_z_rows = [2.5, -2.5]   # 上下2箇所(仮)。ステーの実配置が決まれば変更
    pin_hole_depth = pin_boss_total + 0.2  # 補強ボスをクリーンカットで貫通

    for seg, sign in [(left_seg, -1), (right_seg, 1)]:
        # 補強ボス(既存の端面肉厚の外側にUNIONで追加。内部空洞側へは食い込ませない)
        boss_inner_x = sign * (case_outer_x - pin_wall_inner)  # 既存肉厚の内側境界(空洞側の境界と面一)
        boss_outer_x = sign * (case_outer_x + pin_boss_extra)  # 新しい外側の面
        boss_x = (boss_inner_x + boss_outer_x) / 2

        for push_pin_z in push_pin_z_rows:
            bpy.ops.mesh.primitive_cube_add(size=1, location=(boss_x, push_pin_y, push_pin_z))
            boss = bpy.context.active_object
            boss.scale = (pin_boss_total, pin_boss_size_y, pin_boss_size_z)
            bpy.ops.object.transform_apply(scale=True)
            mod = seg.modifiers.new(name="PinBoss", type='BOOLEAN')
            mod.object = boss; mod.operation = 'UNION'
            bpy.context.view_layer.objects.active = seg; bpy.ops.object.modifier_apply(modifier="PinBoss")
            bpy.data.objects.remove(boss, do_unlink=True)

            # プッシュピン穴(側面を貫通、X軸方向)
            bpy.ops.mesh.primitive_cylinder_add(radius=push_pin_dia/2, depth=pin_hole_depth,
                                                 location=(boss_x, push_pin_y, push_pin_z),
                                                 rotation=(0, math.radians(90), 0))
            pin_hole = bpy.context.active_object
            mod = seg.modifiers.new(name="PushPinHole", type='BOOLEAN')
            mod.object = pin_hole; mod.operation = 'DIFFERENCE'
            bpy.context.view_layer.objects.active = seg; bpy.ops.object.modifier_apply(modifier="PushPinHole")
            bpy.data.objects.remove(pin_hole, do_unlink=True)

    print(f"Model Ready: Mount block spacing (center-to-center) is {mount_offset_x * 2} cm.")

if __name__ == "__main__":
    create_mgts_model()
