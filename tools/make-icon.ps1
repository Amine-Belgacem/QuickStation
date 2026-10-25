$ErrorActionPreference = 'Stop'
# Image-asset generator only; QuickStation uses native Win32 controls at runtime.
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$source = [Drawing.Bitmap]::new((Join-Path $root 'assets\quickstation-logo.png'))
if ($source.Width -ne 512 -or $source.Height -ne 512) {
    $source.Dispose()
    throw 'The QuickStation logo source must be a 512 x 512 PNG.'
}
$sizes = @(16,20,24,32,48,64,128,256)
$frames = @()
foreach ($size in $sizes) {
    $image = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($image)
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.DrawImage($source,0,0,$size,$size)
    $graphics.Dispose()
    $stream = [IO.MemoryStream]::new();$writer = [IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]40);$writer.Write([int32]$size);$writer.Write([int32]($size*2))
    $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]0);$writer.Write([uint32]($size*$size*4))
    1..4 | ForEach-Object { $writer.Write([uint32]0) }
    for ($y=$size-1;$y -ge 0;$y--) {
        for ($x=0;$x -lt $size;$x++) {
            $color=$image.GetPixel($x,$y)
            $writer.Write([byte]$color.B);$writer.Write([byte]$color.G);$writer.Write([byte]$color.R);$writer.Write([byte]$color.A)
        }
    }
    $maskStride = [int]([Math]::Ceiling($size/32.0)*4)
    $writer.Write([byte[]]::new($maskStride*$size));$writer.Flush()
    $frames += ,$stream.ToArray()
    $writer.Dispose();$stream.Dispose();$image.Dispose()
}
$source.Dispose()
$output = [IO.MemoryStream]::new();$writer = [IO.BinaryWriter]::new($output)
$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
$offset = 6+16*$sizes.Count
for ($i=0;$i -lt $sizes.Count;$i++) {
    $entrySize = if ($sizes[$i] -eq 256) { [byte]0 } else { [byte]$sizes[$i] }
    $writer.Write($entrySize);$writer.Write($entrySize);$writer.Write([byte]0);$writer.Write([byte]0)
    $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset)
    $offset += $frames[$i].Length
}
foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
$writer.Flush();[IO.File]::WriteAllBytes((Join-Path $root 'assets\quickstation.ico'),$output.ToArray())
$writer.Dispose();$output.Dispose()

