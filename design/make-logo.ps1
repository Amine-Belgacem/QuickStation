$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Color([string]$value) { [Drawing.ColorTranslator]::FromHtml($value) }
function RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$radius) {
    $p = [Drawing.Drawing2D.GraphicsPath]::new()
    $d = 2 * $radius
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

$blue = Color '#187BC4'
$deep = Color '#105080'
$white = Color '#FFFFFF'
$gold = Color '#FFB33E'
$ink = Color '#17324E'
$muted = Color '#607286'
$icon = [Drawing.Bitmap]::new(512, 512, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [Drawing.Graphics]::FromImage($icon)
$g.Clear([Drawing.Color]::Transparent)
$g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.ScaleTransform(4, 4)

# The antenna carries the speed cue. The rest of the canvas is transparent.
$antenna = [Drawing.PointF[]]@(
    [Drawing.PointF]::new(27, 51), [Drawing.PointF]::new(55, 17),
    [Drawing.PointF]::new(66, 17), [Drawing.PointF]::new(76, 4),
    [Drawing.PointF]::new(69, 31), [Drawing.PointF]::new(57, 31),
    [Drawing.PointF]::new(44, 51))
$g.FillPolygon([Drawing.SolidBrush]::new($gold), $antenna)

$receiver = RoundedPath 4 44 120 79 17
$g.FillPath([Drawing.SolidBrush]::new($blue), $receiver)
$receiver.Dispose()

# Broad elements survive the 16-pixel Windows title-bar size.
$g.FillEllipse([Drawing.SolidBrush]::new($deep), 19, 57, 54, 54)
$g.FillEllipse([Drawing.SolidBrush]::new($white), 24, 62, 44, 44)
$g.FillEllipse([Drawing.SolidBrush]::new($deep), 33, 71, 26, 26)
$g.FillEllipse([Drawing.SolidBrush]::new($gold), 40, 78, 12, 12)
foreach ($y in @(65, 80, 95)) {
    $bar = RoundedPath 81 $y 28 7 3.5
    $g.FillPath([Drawing.SolidBrush]::new($white), $bar)
    $bar.Dispose()
}
$g.Dispose()
$root = Split-Path -Parent $PSScriptRoot
$icon.Save((Join-Path $root 'assets\quickstation-logo.png'), [Drawing.Imaging.ImageFormat]::Png)

$preview = [Drawing.Bitmap]::new(1100, 600, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$p = [Drawing.Graphics]::FromImage($preview)
$p.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$p.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$p.Clear((Color '#F1F4F7'))
$card = RoundedPath 34 34 1032 532 24
$p.FillPath([Drawing.SolidBrush]::new($white), $card)
$card.Dispose()
$p.DrawImage($icon, 69, 102, 308, 308)

$heading = [Drawing.Font]::new('Segoe UI Semibold', 37)
$sub = [Drawing.Font]::new('Segoe UI', 19)
$body = [Drawing.Font]::new('Segoe UI', 13)
$small = [Drawing.Font]::new('Segoe UI', 11)
$p.DrawString('QuickStation', $heading, [Drawing.SolidBrush]::new($ink), 400, 98)
$p.DrawString('The radio is the icon', $sub, [Drawing.SolidBrush]::new($deep), 402, 172)
$p.DrawString('No square background. More room for the receiver.', $body,
              [Drawing.SolidBrush]::new($muted), 402, 225)

$p.DrawString('Windows sizes', $small, [Drawing.SolidBrush]::new($ink), 403, 291)
$x = 403
foreach ($size in @(48, 32, 24, 16)) {
    $tile = RoundedPath $x 323 104 105 10
    $p.FillPath([Drawing.SolidBrush]::new((Color '#F1F4F7')), $tile)
    $tile.Dispose()
    $tiny = [Drawing.Bitmap]::new($size, $size)
    $tg = [Drawing.Graphics]::FromImage($tiny)
    $tg.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $tg.DrawImage($icon, 0, 0, $size, $size)
    $tg.Dispose()
    $p.DrawImage($tiny, $x + (104 - $size) / 2, 338 + (48 - $size) / 2, $size, $size)
    $tiny.Dispose()
    $p.DrawString("$size px", $small, [Drawing.SolidBrush]::new($muted), $x + 29, 395)
    $x += 119
}

$darkTile = RoundedPath 401 453 462 82 12
$p.FillPath([Drawing.SolidBrush]::new((Color '#243549')), $darkTile)
$darkTile.Dispose()
$p.DrawImage($icon, 421, 466, 50, 50)
$p.DrawString('Also clear on a dark title bar', $small,
              [Drawing.SolidBrush]::new($white), 489, 482)
$p.DrawString('Transparent PNG', $small, [Drawing.SolidBrush]::new($muted), 75, 475)

$p.Dispose()
$preview.Save((Join-Path $PSScriptRoot 'quickstation-logo-preview.png'), [Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()
$icon.Dispose()
$heading.Dispose(); $sub.Dispose(); $body.Dispose(); $small.Dispose()
