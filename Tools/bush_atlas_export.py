# Writes the DZ tree packs' leaf atlases as TGA for Tools/bush_build.py (which reads only their alpha). Run by Tools/BuildBushes.ps1.
import unreal, os
out = os.environ.get('JEEB_BUSH_TEX', '').replace('\\', '/')
TX = '/Game/DZ_Assets/DZ_Trees/Textures/Leaves_Needles/'
for n in ['T_Cork_Oak_Leaves_Diffuse', 'T_Aspen_Leaves_Diffuse', 'T_Aspen_Leaves_Small_Diffuse']:
    t = unreal.AssetExportTask()
    t.object = unreal.load_asset(TX + n)
    t.filename = out + '/' + n + '.tga'
    t.automated = True; t.prompt = False; t.replace_identical = True
    t.exporter = unreal.TextureExporterTGA()
    unreal.log('BUSHATLAS %s ok=%s' % (n, unreal.Exporter.run_asset_export_task(t)))
