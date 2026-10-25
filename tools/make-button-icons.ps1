$ErrorActionPreference = 'Stop'
# Build-time ordinary icon assets only. The app uses native BUTTON painting.
Add-Type -AssemblyName System.Drawing
$iconRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\buttons'
New-Item -ItemType Directory -Force -Path $iconRoot | Out-Null
$sizes = @(16,20,24,32,48,64)
foreach ($light in @($false,$true)) {
    $color = if ($light) { [Drawing.Color]::White } else { [Drawing.Color]::Black }
    foreach ($symbol in @('play','stop','speaker','muted')) {
        $frames = @()
        foreach ($size in $sizes) {
            $image = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [Drawing.Graphics]::FromImage($image)
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.ScaleTransform($size/16.0,$size/16.0)
            $brush = [Drawing.SolidBrush]::new($color)
            $pen = [Drawing.Pen]::new($color,1.5)
            $pen.StartCap = [Drawing.Drawing2D.LineCap]::Round
            $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
            if ($symbol -eq 'play') {
                $graphics.FillPolygon($brush,[Drawing.PointF[]]@([Drawing.PointF]::new(4,3),[Drawing.PointF]::new(13,8),[Drawing.PointF]::new(4,13)))
            } elseif ($symbol -eq 'stop') {
                $graphics.FillRectangle($brush,[single]4,[single]4,[single]8,[single]8)
            } else {
                $graphics.FillPolygon($brush,[Drawing.PointF[]]@([Drawing.PointF]::new(2,6),[Drawing.PointF]::new(5,6),[Drawing.PointF]::new(8,3),[Drawing.PointF]::new(8,13),[Drawing.PointF]::new(5,10),[Drawing.PointF]::new(2,10)))
                if ($symbol -eq 'speaker') {
                    $graphics.DrawArc($pen,[single]5,[single]3,[single]10,[single]10,[single]-55,[single]110)
                    $graphics.DrawArc($pen,[single]6,[single]5,[single]6,[single]6,[single]-55,[single]110)
                } else {
                    $graphics.DrawLine($pen,[single]10,[single]5,[single]14,[single]11)
                    $graphics.DrawLine($pen,[single]14,[single]5,[single]10,[single]11)
                }
            }
            $pen.Dispose(); $brush.Dispose(); $graphics.Dispose()
            $frameStream = [IO.MemoryStream]::new()
            $image.Save($frameStream,[Drawing.Imaging.ImageFormat]::Png)
            $frames += ,$frameStream.ToArray()
            $frameStream.Dispose(); $image.Dispose()
        }
        $output = [IO.MemoryStream]::new()
        $writer = [IO.BinaryWriter]::new($output)
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6+16*$sizes.Count
        for ($index=0;$index -lt $sizes.Count;$index++) {
            $writer.Write([byte]$sizes[$index]); $writer.Write([byte]$sizes[$index])
            $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$index].Length); $writer.Write([uint32]$offset)
            $offset += $frames[$index].Length
        }
        foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
        $writer.Flush()
        $suffix = if ($light) { '-white' } else { '' }
        [IO.File]::WriteAllBytes((Join-Path $iconRoot ($symbol+$suffix+'.ico')),$output.ToArray())
        $writer.Dispose(); $output.Dispose()
    }
}
