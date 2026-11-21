# The spruces' cones, repainted dark brown-green (owner, 2026-11-21: "less orange and more dark brown/green"). Run once per machine,
# with the Unreal editor closed for this project folder (like Tools/BuildGrassPacks.ps1): the Spruce Forest pack is not in git, and
# neither are the copies this makes (Content/TreeGroves/SpruceCones, git-ignored).
#   powershell -ExecutionPolicy Bypass -File Tools\BuildSpruceCones.ps1
# What it does: (1) Unreal writes the pack's needle and far-card colour textures to Saved/SpruceCones/<name>.png; (2) every orange
# texel (the cones; the needles are green) is repainted here, keeping its own light and dark; (3) Unreal brings the results in as
# /Game/TreeGroves/SpruceCones/<name>_Cones with the pack texture's settings. The pack's own files are never changed. The game swaps
# them in at run time (JeebTreeLook::DarkCones); without them the spruces keep the pack's cones and the log says so (JEEBSPRUCE).
# -OnlyPaint repaints the PNGs already in Saved/SpruceCones (to try other numbers) without running Unreal.
param([switch]$OnlyPaint)
$proj = Split-Path $PSScriptRoot -Parent
if (-not $OnlyPaint -and (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') })) {
    Write-Host 'The Unreal editor is still open. Close it first.'
    exit 1
}
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = (Join-Path $PSScriptRoot 'spruce_cones.py').Replace('\', '/')
$dir = Join-Path $proj 'Saved\SpruceCones'
function Run-Step($step) {
    $out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" "-ConesStep=$step" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false   # (the last two: no waiting at start-up for another copy's build)
    $out | Select-String -Pattern 'CONES (export|import)|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
    return [bool]($out -match "CONES $step done")
}
if (-not $OnlyPaint) { if (-not (Run-Step 'export')) { Write-Host 'Spruce cones: export FAILED.'; exit 1 } }

# The repaint. A cone texel is orange: its hue between red-orange and yellow-orange and strongly coloured; the needles (yellow-green to
# green), the grey-brown twigs and bark and the far cards' dull brown padding are left alone. Each cone texel goes to the cone colour times its own brightness against a typical
# cone's, so the scales' light and dark stay. The mask is soft at both ends, so no hard outline is left round a cone.
# Cone colour (the texture's, before the game's needle tint, which has no blue): a dark warm brown; with the tint it reads dark brown-green.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class JeebCones
{
    static double Smooth(double a, double b, double x) { double t = Math.Max(0, Math.Min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); }
    // returns the share of texels repainted
    // a box blur of the colour channels (not alpha) along rows or columns, radius r texels
    static void Blur(byte[] px, int w, int h, int stride, int r, bool rows)
    {
        int len = rows ? w : h, lines = rows ? h : w;
        double[] acc = new double[3 * len];
        for (int L = 0; L < lines; L++)
        {
            for (int c = 0; c < 3; c++)
            {
                double sum = 0; int cnt = 0;
                for (int t = -r; t < len + r; t++)
                {
                    int add = t + r, rem = t - r - 1;
                    if (add < len) { sum += px[Idx(rows, L, add, stride) + c]; cnt++; }
                    if (rem >= 0) { sum -= px[Idx(rows, L, rem, stride) + c]; cnt--; }
                    if (t >= 0 && t < len) acc[3 * t + c] = sum / cnt;
                }
            }
            for (int t = 0; t < len; t++) for (int c = 0; c < 3; c++) px[Idx(rows, L, t, stride) + c] = (byte)Math.Round(acc[3 * t + c]);
        }
    }
    static int Idx(bool rows, int L, int t, int stride) { return rows ? L * stride + t * 4 : t * stride + L * 4; }
    public static double Paint(string src, string dst, double hueLo, double hueHi, double hueSoft, double satLo, double satHi, int tr, int tg, int tb, double vref, bool far, double farLo, int blur)
    {
        using (Bitmap inB = new Bitmap(src))
        using (Bitmap bmp = inB.Clone(new Rectangle(0, 0, inB.Width, inB.Height), PixelFormat.Format32bppArgb))
        {
            BitmapData d = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
            int n = d.Stride * d.Height;
            byte[] px = new byte[n];
            Marshal.Copy(d.Scan0, px, 0, n);
            double changed = 0;
            for (int i = 0; i + 3 < n; i += 4)
            {
                double b = px[i], g = px[i + 1], r = px[i + 2];
                double mx = Math.Max(r, Math.Max(g, b)), mn = Math.Min(r, Math.Min(g, b));
                if (mx < 8 || mx - mn < 1) continue;
                double sat = (mx - mn) / mx;
                double hue;
                if (mx == r) hue = 60.0 * ((g - b) / (mx - mn));
                else if (mx == g) hue = 60.0 * (2.0 + (b - r) / (mx - mn));
                else hue = 60.0 * (4.0 + (r - g) / (mx - mn));
                if (hue < -180) hue += 360; if (hue > 180) hue -= 360;
                double m = Smooth(hueLo - hueSoft, hueLo, hue) * (1.0 - Smooth(hueHi, hueHi + hueSoft, hue)) * Smooth(satLo, satHi, sat);
                if (m <= 0.003) continue;
                double k = Math.Max(0.45, Math.Min(1.6, mx / vref));
                px[i] = (byte)Math.Round(b + (Math.Min(255.0, tb * k) - b) * m);
                px[i + 1] = (byte)Math.Round(g + (Math.Min(255.0, tg * k) - g) * m);
                px[i + 2] = (byte)Math.Round(r + (Math.Min(255.0, tr * k) - r) * m);
                changed += m;
            }
            if (far)
            {
                // the far cards (2026-11-21, owner: "too much detail and bright orange pinecones"): the cones' sunlit yellow tips the orange
                // rule leaves (bright, hue 34-62) go the cone brown too, then the colour is softened by a small box blur, so at the switch
                // distance the card reads like the near model's soft mass of needles rather than a sharp picture of every twig
                for (int i = 0; i + 3 < n; i += 4)
                {
                    double b = px[i], g = px[i + 1], r = px[i + 2];
                    double mx = Math.Max(r, Math.Max(g, b)), mn = Math.Min(r, Math.Min(g, b));
                    if (mx < 8 || mx - mn < 1 || (mx != r && mx != g)) continue;
                    double hue = mx == r ? 60.0 * ((g - b) / (mx - mn)) : 60.0 * (2.0 + (b - r) / (mx - mn));
                    double m = Smooth(34, 40, hue) * (1.0 - Smooth(56, 62, hue)) * Smooth(0.55, 0.7, (mx - mn) / mx) * Smooth(farLo, farLo + 40, mx);
                    if (m <= 0.003) continue;
                    double k = Math.Max(0.45, Math.Min(1.6, mx / vref));
                    px[i] = (byte)Math.Round(b + (Math.Min(255.0, tb * k) - b) * m);
                    px[i + 1] = (byte)Math.Round(g + (Math.Min(255.0, tg * k) - g) * m);
                    px[i + 2] = (byte)Math.Round(r + (Math.Min(255.0, tr * k) - r) * m);
                }
                if (blur > 0) { Blur(px, bmp.Width, bmp.Height, d.Stride, blur, true); Blur(px, bmp.Width, bmp.Height, d.Stride, blur, false); }
            }
            Marshal.Copy(px, 0, d.Scan0, n);
            bmp.UnlockBits(d);
            bmp.Save(dst, ImageFormat.Png);
            return changed / (bmp.Width * bmp.Height);
        }
    }
    // (2026-11-21 evening, owner: the far card read as "a blurry solid cone" next to the near model's "airy drooping fronds with sky
    // showing through") the far card's opacity (the Masks sheet's red channel) is cut where its colour picture is darker than its
    // surroundings (the shaded gaps between the fronds), so sky shows through between the branches; returns the share of opacity removed
    public static double Airy(string mask, string albedo, string dst, int radius, double lo, double hi, double strength)
    {
        using (Bitmap mB = new Bitmap(mask)) using (Bitmap aB = new Bitmap(albedo))
        using (Bitmap m = mB.Clone(new Rectangle(0, 0, mB.Width, mB.Height), PixelFormat.Format32bppArgb))
        using (Bitmap aS = new Bitmap(aB, mB.Width, mB.Height)) using (Bitmap a = aS.Clone(new Rectangle(0, 0, aS.Width, aS.Height), PixelFormat.Format32bppArgb))
        {
            if (m.Width != a.Width || m.Height != a.Height) return -1;
            BitmapData dm = m.LockBits(new Rectangle(0, 0, m.Width, m.Height), ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
            BitmapData da = a.LockBits(new Rectangle(0, 0, a.Width, a.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            int n = dm.Stride * dm.Height;
            byte[] pm = new byte[n], pa = new byte[n];
            Marshal.Copy(dm.Scan0, pm, 0, n); Marshal.Copy(da.Scan0, pa, 0, n);
            a.UnlockBits(da);
            // the albedo's brightness, in all three channels, then its local mean
            byte[] lum = new byte[n], mean = new byte[n];
            for (int i = 0; i + 3 < n; i += 4) { byte v = (byte)Math.Round(0.3 * pa[i + 2] + 0.59 * pa[i + 1] + 0.11 * pa[i]); lum[i] = lum[i + 1] = lum[i + 2] = v; }
            Blur(lum, m.Width, m.Height, dm.Stride, Math.Max(1, radius / 6), true); Blur(lum, m.Width, m.Height, dm.Stride, Math.Max(1, radius / 6), false);   // (gaps a frond wide, not single needles)
            Array.Copy(lum, mean, n);
            Blur(mean, m.Width, m.Height, dm.Stride, radius, true); Blur(mean, m.Width, m.Height, dm.Stride, radius, false);
            double before = 0, after = 0;
            for (int i = 0; i + 3 < n; i += 4)
            {
                double op = pm[i + 2];
                if (op < 1) continue;
                double ratio = lum[i] / Math.Max(4.0, (double)mean[i]);
                double keep = 1.0 - strength * (1.0 - Smooth(lo, hi, ratio));
                before += op; after += op * keep;
                pm[i + 2] = (byte)Math.Round(op * keep);
            }
            Marshal.Copy(pm, 0, dm.Scan0, n);
            m.UnlockBits(dm);
            m.Save(dst, ImageFormat.Png);
            return before > 0 ? 1.0 - after / before : 0;
        }
    }
}
'@
# hue from, hue to, softness (degrees: 0 red, 30 orange, 60 yellow, 120 green); saturation from, full by; cone colour (sRGB 0-255); a typical cone texel's brightest channel
$HueLo = 8; $HueHi = 38; $HueSoft = 7; $SatLo = 0.56; $SatHi = 0.74; $Cone = 84, 68, 50; $VRef = 190
# far cards only: the yellow cone tips repainted from this brightness up (0-255); box blur radius in texels of the 4096 sheet (0: none)
# (2026-11-21 evening: no blur; the owner found the soft card "blurry")
$FarLo = 110; $FarBlur = 0
# far cards' see-through gaps: cut opacity where the colour is below this share of its surroundings (soft from Lo to Hi), over this
# radius in texels of the 4096 sheet, by this much (0 none, 1 fully)
$AirLo = 0.82; $AirHi = 0.97; $AirRadius = 60; $AirStrength = 1.0
$n = 0
Get-ChildItem $dir -Filter '*_imposter_O.png' | ForEach-Object {
    $alb = Get-ChildItem $dir -Filter ($_.BaseName.Replace('_imposter_O', '_imposter_A') + '*.png') | Where-Object { $_.BaseName -notlike '*_Cones' } | Select-Object -First 1
    if (-not $alb) { Write-Host "CONES no colour sheet for $($_.BaseName)"; return }
    $share = [JeebCones]::Airy($_.FullName, $alb.FullName, (Join-Path $dir ($_.BaseName + '_Cones.png')), $AirRadius, $AirLo, $AirHi, $AirStrength)
    Write-Host ('CONES opened {0}: {1:P1} of its opacity removed' -f $_.BaseName, $share)
}
Get-ChildItem $dir -Filter '*.png' | Where-Object { $_.BaseName -notlike '*_Cones' -and $_.BaseName -notlike '*_imposter_O' } | ForEach-Object {
    $share = [JeebCones]::Paint($_.FullName, (Join-Path $dir ($_.BaseName + '_Cones.png')), $HueLo, $HueHi, $HueSoft, $SatLo, $SatHi, $Cone[0], $Cone[1], $Cone[2], $VRef, ($_.BaseName -like '*imposter*'), $FarLo, $FarBlur)
    Write-Host ('CONES painted {0}: {1:P2} of its texels' -f $_.BaseName, $share)
    $n++
}
if ($n -eq 0) { Write-Host 'Spruce cones: no pictures to repaint.'; exit 1 }
if ($OnlyPaint) { exit 0 }
if (Run-Step 'import') { Write-Host 'Spruce cones built.'; exit 0 } else { Write-Host 'Spruce cones: import FAILED.'; exit 1 }
