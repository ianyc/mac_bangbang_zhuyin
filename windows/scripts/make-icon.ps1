<#
.SYNOPSIS
  產生輸入法圖示 windows\tip\BangbangZhuyin.ico：藍底白字「棒」，16、20、24、32、40、48 像素。
  對應 Mac 版的 scripts/make-icon.swift。圖示已經放在 repo 裡，只有要改圖示時才需要重新執行。

.EXAMPLE
  windows\scripts\make-icon.ps1
#>
param(
    [string] $Output = (Join-Path (Split-Path -Parent $PSScriptRoot) 'tip\BangbangZhuyin.ico')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-IconBitmap([int] $size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)

    # 圓角方塊
    $r = [Math]::Max(2.0, $size * 0.22)
    $d = $r * 2
    $s = $size - 1
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($s - $d, 0, $d, $d, 270, 90)
    $path.AddArc($s - $d, $s - $d, $d, $d, 0, 90)
    $path.AddArc(0, $s - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 37, 99, 235))
    $g.FillPath($brush, $path)

    # 「棒」字
    $font = New-Object System.Drawing.Font('Microsoft JhengHei UI', [single]($size * 0.72), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF(0, [single]($size * 0.03), [single]$size, [single]$size)
    $g.DrawString('棒', $font, [System.Drawing.Brushes]::White, $rect, $format)

    $font.Dispose(); $brush.Dispose(); $path.Dispose(); $format.Dispose(); $g.Dispose()
    return $bitmap
}

# 把一張圖轉成 ICO 裡的 DIB（BITMAPINFOHEADER + 由下往上的 BGRA 像素 + AND 遮罩），小尺寸相容性最好。
function Get-DibBytes([System.Drawing.Bitmap] $bitmap) {
    $w = $bitmap.Width; $h = $bitmap.Height
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $pixels = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
    $bitmap.UnlockBits($data)

    $maskStride = [int]([Math]::Ceiling($w / 32.0) * 4)
    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter $stream
    $writer.Write([int]40); $writer.Write([int]$w); $writer.Write([int]($h * 2))
    $writer.Write([int16]1); $writer.Write([int16]32); $writer.Write([int]0)
    $writer.Write([int]($w * $h * 4 + $maskStride * $h)); $writer.Write([int]0); $writer.Write([int]0); $writer.Write([int]0); $writer.Write([int]0)
    for ($y = $h - 1; $y -ge 0; $y--) { $writer.Write($pixels, $y * $stride, $w * 4) }
    $writer.Write((New-Object byte[] ($maskStride * $h)))  # 透明度由 alpha 決定，遮罩全為 0
    $writer.Flush()
    return , $stream.ToArray()
}

$sizes = 16, 20, 24, 32, 40, 48
$images = foreach ($size in $sizes) { $b = New-IconBitmap $size; , (Get-DibBytes $b); $b.Dispose() }

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([int16]0); $writer.Write([int16]1); $writer.Write([int16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $writer.Write([byte]$sizes[$i]); $writer.Write([byte]$sizes[$i]); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([int16]1); $writer.Write([int16]32); $writer.Write([int]$images[$i].Length); $writer.Write([int]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
[System.IO.File]::WriteAllBytes($Output, $out.ToArray())
Write-Host "已產生 $Output（$($out.Length) bytes）"
