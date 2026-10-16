# Genera resources/app.ico (multitamano, PNG embebido) sin dependencias externas.
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent

function New-IconBitmap([int]$size) {
    $bmp = [System.Drawing.Bitmap]::new($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    [double]$s = $size
    [double]$margin = $s * 0.055
    [double]$w = $s - ($margin * 2.0)
    [double]$radius = $s * 0.22

    $rect = [System.Drawing.RectangleF]::new([float]$margin, [float]$margin, [float]$w, [float]$w)

    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    [double]$d = $radius * 2.0
    $path.AddArc([float]$rect.X, [float]$rect.Y, [float]$d, [float]$d, 180, 90)
    $path.AddArc([float]($rect.Right - $d), [float]$rect.Y, [float]$d, [float]$d, 270, 90)
    $path.AddArc([float]($rect.Right - $d), [float]($rect.Bottom - $d), [float]$d, [float]$d, 0, 90)
    $path.AddArc([float]$rect.X, [float]($rect.Bottom - $d), [float]$d, [float]$d, 90, 90)
    $path.CloseFigure()

    $c1 = [System.Drawing.Color]::FromArgb(255, 27, 40, 56)
    $c2 = [System.Drawing.Color]::FromArgb(255, 10, 16, 24)
    $brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $c1, $c2, 45.0)
    $g.FillPath($brush, $path)

    # Anillo azul (estilo Steam) que sugiere actividad reciente
    [double]$ring = $s * 0.60
    [double]$ringX = ($s - $ring) / 2.0
    $ringRect = [System.Drawing.RectangleF]::new([float]$ringX, [float]$ringX, [float]$ring, [float]$ring)
    $ringPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 102, 192, 244), [float]($s * 0.085))
    $ringPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $ringPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawArc($ringPen, $ringRect, -70.0, 300.0)

    # Agujas del reloj
    [double]$cx = $s / 2.0
    $handPen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, [float]($s * 0.075))
    $handPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $handPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($handPen, [float]$cx, [float]$cx, [float]$cx, [float]($s * 0.30))
    $g.DrawLine($handPen, [float]$cx, [float]$cx, [float]($s * 0.67), [float]($cx + $s * 0.09))

    [double]$dot = $s * 0.09
    $dotRect = [System.Drawing.RectangleF]::new([float]($cx - $dot / 2.0), [float]($cx - $dot / 2.0), [float]$dot, [float]$dot)
    $g.FillEllipse([System.Drawing.Brushes]::White, $dotRect)

    $brush.Dispose(); $ringPen.Dispose(); $handPen.Dispose(); $path.Dispose(); $g.Dispose()
    return $bmp
}

$sizes = @(256, 128, 64, 48, 32, 24, 16)
$pngData = @()
foreach ($sz in $sizes) {
    $bmp = New-IconBitmap $sz
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngData += , $ms.ToArray()
    $ms.Dispose(); $bmp.Dispose()
}

$outDir = Join-Path $root "resources"
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
$outPath = Join-Path $outDir "app.ico"

$fs = [System.IO.File]::Create($outPath)
$bw = [System.IO.BinaryWriter]::new($fs)
$bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz = $sizes[$i]
    $dim = 0
    if ($sz -lt 256) { $dim = $sz }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]$pngData[$i].Length)
    $bw.Write([int]$offset)
    $offset += $pngData[$i].Length
}
foreach ($d in $pngData) { $bw.Write([byte[]]$d) }
$bw.Flush(); $bw.Dispose(); $fs.Dispose()
Write-Host "Icono generado: $outPath ($([math]::Round((Get-Item $outPath).Length/1KB,1)) KB)"
