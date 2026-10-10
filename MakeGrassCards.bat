@echo off
echo One-time setup for the card grass (close the Unreal editor first).
echo This paints a grass texture and builds the material. Wait for "Setup finished".
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m='###'+'CODE###'; $t=Get-Content -LiteralPath '%~f0' -Raw; Invoke-Expression (($t -split [regex]::Escape($m))[1])"
echo.
pause
exit /b
###CODE###
$ErrorActionPreference = 'Stop'
$proj = 'C:\Users\johnl\Documents\Unreal Projects\Jeebsland'
$up = Join-Path $proj 'Jeebsland.uproject'

if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) {
    Write-Host 'The Unreal editor is still open. Close it completely (check the system tray too), then run this again.'
    return
}
# start fresh so an older half-made copy cannot get in the way
foreach ($f in @('M_Grass.uasset','T_GrassTuft.uasset','M_Bush.uasset','T_BushLeaves.uasset','M_TreeLeaf.uasset','T_TreeLeaves.uasset','M_BarkTint.uasset')) { $p = Join-Path $proj ('Content\' + $f); if (Test-Path $p) { Remove-Item -LiteralPath $p -Force } }

# 1) Paint a tuft of grass blades on a see-through picture
Add-Type -AssemblyName System.Drawing
$png = Join-Path $env:TEMP 'T_GrassTuft.png'
$bmp = New-Object System.Drawing.Bitmap 256, 128, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.Clear([System.Drawing.Color]::FromArgb(0, 40, 120, 30))
$rnd = New-Object System.Random 7
$tip = [System.Drawing.Color]::FromArgb(255, 125, 148, 55)
$root = [System.Drawing.Color]::FromArgb(255, 36, 58, 22)
for ($i = 0; $i -lt 12; $i++) {
    $bx = 16 + $i * 20 + $rnd.Next(-6, 7)
    $h = 70 + $rnd.Next(0, 52)
    $lean = $rnd.Next(-38, 39)
    $w = 8
    $tx = $bx + $lean; $ty = 127 - $h
    $mx = $bx + $lean * 0.3; $my = 127 - $h * 0.55
    [System.Drawing.PointF[]]$pts = @(
        [System.Drawing.PointF]::new([single]($bx - $w), [single]127),
        [System.Drawing.PointF]::new([single]($mx - $w * 0.6), [single]$my),
        [System.Drawing.PointF]::new([single]$tx, [single]$ty),
        [System.Drawing.PointF]::new([single]($mx + $w * 0.6), [single]$my),
        [System.Drawing.PointF]::new([single]($bx + $w), [single]127))
    $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.PointF]::new(0, [single]$ty), [System.Drawing.PointF]::new(0, [single]127), $tip, $root)
    $g.FillPolygon($br, $pts)
    $br.Dispose()
}
$g.Dispose()
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host 'Painted the grass picture.'

# 1b) Paint a leafy sprig (with a few berries) for the bushes
$png2 = Join-Path $env:TEMP 'T_BushLeaves.png'
$bmp2 = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.SmoothingMode = 'AntiAlias'
$g2.Clear([System.Drawing.Color]::FromArgb(0, 40, 70, 25))
$rnd2 = New-Object System.Random 11
for ($i = 0; $i -lt 46; $i++) {
    $ang = $rnd2.Next(-80, 81)
    $dist = $rnd2.Next(25, 205)
    $cx = 128 + [math]::Sin($ang * [math]::PI / 180) * $dist * 0.95
    $cy = 248 - [math]::Cos($ang * [math]::PI / 180) * $dist * 0.88
    $t = $dist / 205.0
    if ($rnd2.Next(0, 100) -lt 9) {
        $c = [System.Drawing.Color]::FromArgb(255, [int](120 + 50 * $t), [int](60 + 25 * $t), 24)      # a few russet autumn leaves
    } else {
        $c = [System.Drawing.Color]::FromArgb(255, [int](40 + 55 * $t), [int](70 + 70 * $t), [int](22 + 14 * $t))
    }
    $br = New-Object System.Drawing.SolidBrush $c
    $st = $g2.Save()
    $g2.TranslateTransform([single]$cx, [single]$cy)
    $g2.RotateTransform([single]($ang + $rnd2.Next(-25, 26)))
    $g2.FillEllipse($br, -12, -26, 24, 52)
    $g2.Restore($st)
    $br.Dispose()
}
$berry = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 150, 28, 36))
foreach ($b in @(@(90,120),@(160,100),@(120,170),@(185,150),@(70,185))) { $g2.FillEllipse($berry, [single]$b[0], [single]$b[1], 11, 11) }
$g2.Dispose()
$bmp2.Save($png2, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp2.Dispose()
Write-Host 'Painted the bush picture.'

# 1c) Paint a cluster of plain pale leaves for the trees (the game colours them red, orange, amber and gold)
$png3 = Join-Path $env:TEMP 'T_TreeLeaves.png'
$bmp3 = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g3 = [System.Drawing.Graphics]::FromImage($bmp3)
$g3.SmoothingMode = 'AntiAlias'
$g3.Clear([System.Drawing.Color]::FromArgb(0, 200, 190, 180))
$rnd3 = New-Object System.Random 29
for ($i = 0; $i -lt 40; $i++) {
    $cx = 26 + $rnd3.Next(0, 204)
    $cy = 26 + $rnd3.Next(0, 204)
    $v = $rnd3.Next(150, 256)
    $c = [System.Drawing.Color]::FromArgb(255, $v, [int]($v * 0.95), [int]($v * 0.88))
    $br = New-Object System.Drawing.SolidBrush $c
    $st = $g3.Save()
    $g3.TranslateTransform([single]$cx, [single]$cy)
    $g3.RotateTransform([single]($rnd3.Next(0, 180)))
    $g3.FillEllipse($br, -11, -24, 22, 48)
    $g3.Restore($st)
    $br.Dispose()
}
$g3.Dispose()
$bmp3.Save($png3, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp3.Dispose()
Write-Host 'Painted the tree leaf picture.'

# 2) The little script that imports it and builds the material
$py = Join-Path $env:TEMP 'make_grass.py'
$pyText = @'
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary

def import_tex(path, name):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/Game"
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset("/Game/" + name)
    unreal.log("GRASSDIAG texture " + name + ": " + str(tex))
    if tex is None:
        raise RuntimeError("GRASSDIAG the picture " + name + " did not import")
    try:
        tex.set_editor_property("compression_no_alpha", False)
        tex.set_editor_property("srgb", True)
    except Exception as e:
        unreal.log("GRASSDIAG texture settings: " + str(e))
    return tex

def build_material(name, tex, tint_rgb, amp, param=False, gain=1.0):
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + name):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + name)
    mat = tools.create_asset(name, "/Game", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("opacity_mask_clip_value", 0.4)
    for flag in ("used_with_instanced_static_meshes", "used_with_static_lighting", "used_with_nanite"):
        try:
            mat.set_editor_property(flag, flag != "used_with_nanite")
        except Exception as e:
            unreal.log("GRASSDIAG flag " + flag + ": " + str(e))

    ts = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 0)
    ts.set_editor_property("texture", tex)
    # autumn tint: knocks the green down to a warm olive-gold
    if param:
        tint = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, -200)
        tint.set_editor_property("parameter_name", "Tint")
        tint.set_editor_property("default_value", unreal.LinearColor(*tint_rgb))
    else:
        tint = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, -200)
        tint.set_editor_property("constant", unreal.LinearColor(*tint_rgb))
    col = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, -100)
    lib.connect_material_expressions(ts, "RGB", col, "A")
    lib.connect_material_expressions(tint, "", col, "B")
    if gain != 1.0:
        boosted = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -100)   # the pale leaf picture is darker than a plain colour, so lift it
        lib.connect_material_expressions(col, "", boosted, "A")
        boosted.set_editor_property("const_b", gain)
        col = boosted
    if True:
        lib.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
        lib.connect_material_property(col, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        floor_ = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, -100)   # a faint glow so deep shade is never pitch black
        lib.connect_material_expressions(col, "", floor_, "A")
        floor_.set_editor_property("const_b", 0.07)
        lib.connect_material_property(floor_, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 300)
        rough.set_editor_property("r", 1.0)
        lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        spec = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 400)
        spec.set_editor_property("r", 0.0)
        lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    lib.connect_material_property(ts, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    # wind: breezes that roll across the land now and then, a faint constant stir between them, every tuft fluttering on its own beat
    try:
        def node(cls, x=0, y=0, **props):
            n = lib.create_material_expression(mat, cls, x, y)
            for k, v in props.items():
                n.set_editor_property(k, v)
            return n
        def link(a, b, pin=""):
            lib.connect_material_expressions(a, "", b, pin)
        def mul(a, c):
            n = node(unreal.MaterialExpressionMultiply, const_b=c); link(a, n, "A"); return n
        def add(a, c):
            n = node(unreal.MaterialExpressionAdd, const_b=c); link(a, n, "A"); return n
        def muln(a, b):
            n = node(unreal.MaterialExpressionMultiply); link(a, n, "A"); link(b, n, "B"); return n
        def addn(a, b):
            n = node(unreal.MaterialExpressionAdd); link(a, n, "A"); link(b, n, "B"); return n
        def sine(a):
            n = node(unreal.MaterialExpressionSine, period=1.0); link(a, n); return n
        def mask(src, r, g, b_):
            n = node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b_, a=False); link(src, n); return n

        t = node(unreal.MaterialExpressionTime)
        wp = node(unreal.MaterialExpressionWorldPosition)
        wx = mask(wp, True, False, False)
        wy = mask(wp, False, True, False)
        rnd = node(unreal.MaterialExpressionPerInstanceRandom)

        # two slow waves roll across the map in different directions; where they line up there is a gust
        s1 = sine(addn(addn(mul(wx, 0.00020), mul(wy, 0.00013)), mul(t, -0.04)))
        s2 = sine(addn(addn(mul(wx, 0.00009), mul(wy, -0.00017)), mul(t, 0.017)))
        u1 = add(mul(s1, 0.5), 0.5)
        u2 = add(mul(s2, 0.5), 0.5)
        gust = node(unreal.MaterialExpressionSaturate)
        link(mul(add(muln(u1, u2), -0.25), 2.5), gust)
        gust2 = muln(gust, gust)

        # each tuft flutters on its own random beat
        beat = addn(mul(t, 0.9), addn(mul(rnd, 6.28), mul(wx, 0.01)))
        flutter = sine(beat)
        amp = add(gust2, 0.12)                                   # a little stir all the time, a lot in a gust
        total = addn(muln(flutter, amp), mul(gust2, 0.6))        # sway plus a lean downwind during a gust

        uv = node(unreal.MaterialExpressionTextureCoordinate)
        vm = mask(uv, False, True, False)
        tipm = node(unreal.MaterialExpressionOneMinus); link(vm, tipm)
        off = mul(muln(total, tipm), amp)
        dirv = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(1.0, 0.45, 0.0, 0.0))
        final = muln(off, dirv)
        lib.connect_material_property(final, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    except Exception as e:
        unreal.log("Wind skipped: " + str(e))

    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)

tex_g = import_tex(r"PNGPATH", "T_GrassTuft")
build_material("M_Grass", tex_g, (0.72, 0.68, 0.42, 0.0), 9.0)
unreal.EditorAssetLibrary.save_asset("/Game/T_GrassTuft")
tex_b = import_tex(r"PNGPATH2", "T_BushLeaves")
build_material("M_Bush", tex_b, (0.78, 0.72, 0.50, 0.0), 4.0)
unreal.EditorAssetLibrary.save_asset("/Game/T_BushLeaves")
tex_t = import_tex(r"PNGPATH3", "T_TreeLeaves")
build_material("M_TreeLeaf", tex_t, (1.0, 1.0, 1.0, 0.0), 5.0, True, 0.88)
unreal.EditorAssetLibrary.save_asset("/Game/T_TreeLeaves")

# bark: plain lit two-sided colour with a Tint parameter, for trunks and branches
if unreal.EditorAssetLibrary.does_asset_exist("/Game/M_BarkTint"):
    unreal.EditorAssetLibrary.delete_asset("/Game/M_BarkTint")
bark = tools.create_asset("M_BarkTint", "/Game", unreal.Material, unreal.MaterialFactoryNew())
bark.set_editor_property("two_sided", True)
for flag in ("used_with_instanced_static_meshes", "used_with_static_lighting"):
    try:
        bark.set_editor_property(flag, True)
    except Exception as e:
        unreal.log("GRASSDIAG flag " + flag + ": " + str(e))
bc = lib.create_material_expression(bark, unreal.MaterialExpressionVectorParameter, -500, 0)
bc.set_editor_property("parameter_name", "Tint")
bc.set_editor_property("default_value", unreal.LinearColor(0.16, 0.09, 0.05, 1.0))
lib.connect_material_property(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
br_ = lib.create_material_expression(bark, unreal.MaterialExpressionConstant, -500, 250)
br_.set_editor_property("r", 1.0)
lib.connect_material_property(br_, "", unreal.MaterialProperty.MP_ROUGHNESS)
bs_ = lib.create_material_expression(bark, unreal.MaterialExpressionConstant, -500, 350)
bs_.set_editor_property("r", 0.0)
lib.connect_material_property(bs_, "", unreal.MaterialProperty.MP_SPECULAR)
lib.recompile_material(bark)
unreal.EditorAssetLibrary.save_asset("/Game/M_BarkTint")
unreal.log("M_Grass created")
'@
$pyText.Replace('PNGPATH3', $png3).Replace('PNGPATH2', $png2).Replace('PNGPATH', $png) | Set-Content -LiteralPath $py -Encoding UTF8

# 3) Run it in the editor, without opening a window
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe $up "-ExecutePythonScript=$py" -unattended -nosplash -stdout -FullStdOutLogOutput 2>&1
$out | Select-String -Pattern 'M_Grass|Wind|Traceback|LogPython|GRASSDIAG|T_GrassTuft|Import' | ForEach-Object { Write-Host $_.Line }

if (($out -match 'M_Grass created') -and (Test-Path (Join-Path $proj 'Content\M_Bush.uasset')) -and (Test-Path (Join-Path $proj 'Content\M_TreeLeaf.uasset')) -and (Test-Path (Join-Path $proj 'Content\M_BarkTint.uasset'))) { Write-Host 'Materials created: grass, bush, tree leaf and bark' } else { Write-Host 'The material was NOT created. Send me the lines above.' }
Write-Host 'Setup finished.'
