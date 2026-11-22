<#
.SYNOPSIS
  產生輸入法用的圖示，放在 windows\tip：
  - BangbangZhuyin.ico：藍底白字「棒」（鍵盤清單、語言列）。對應 Mac 版的 scripts/make-icon.swift。
  - mode-zh-light.ico、mode-zh-dark.ico、mode-en-light.ico、mode-en-dark.ico：工作列的「中／英」輸入模式圖示，
    透明背景；light 是淺色工作列用的黑字，dark 是深色工作列用的白字。
  每個圖示都有 16、20、24、32、40、48 像素。圖示已經放在 repo 裡，只有要改圖示時才需要重新執行。

.EXAMPLE
  windows\scripts\make-icon.ps1
#>
param(
    [string] $OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'tip')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48

function New-Canvas([int] $size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)
    return $bitmap, $g
}

function Write-Glyph($g, [int] $size, [string] $glyph, [single] $scale, [System.Drawing.FontStyle] $style, [System.Drawing.Brush] $brush, [single] $offsetY) {
    $font = New-Object System.Drawing.Font('Microsoft JhengHei UI', [single]($size * $scale), $style, [System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF(0, [single]($size * $offsetY), [single]$size, [single]$size)
    $g.DrawString($glyph, $font, $brush, $rect, $format)
    $font.Dispose(); $format.Dispose()
}

# 藍底白字「棒」。
function New-AppIconBitmap([int] $size) {
    $bitmap, $g = New-Canvas $size
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
    Write-Glyph $g $size '棒' 0.72 ([System.Drawing.FontStyle]::Bold) ([System.Drawing.Brushes]::White) 0.03
    $brush.Dispose(); $path.Dispose(); $g.Dispose()
    return $bitmap
}

# 透明背景上的單一個字（「中」或「英」），和系統輸入指示器的其他圖示一樣只有字。
function New-ModeIconBitmap([int] $size, [string] $glyph, [System.Drawing.Brush] $brush) {
    $bitmap, $g = New-Canvas $size
    Write-Glyph $g $size $glyph 1.04 ([System.Drawing.FontStyle]::Regular) $brush 0.03
    $g.Dispose()
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

function Write-Icon([string] $name, [scriptblock] $draw) {
    $images = foreach ($size in $sizes) { $b = & $draw $size; , (Get-DibBytes $b); $b.Dispose() }
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
    $path = Join-Path $OutputDirectory $name
    [System.IO.File]::WriteAllBytes($path, $out.ToArray())
    Write-Host "已產生 $path（$($out.Length) bytes）"
}

$black = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
Write-Icon 'BangbangZhuyin.ico' { param($size) New-AppIconBitmap $size }
Write-Icon 'mode-zh-light.ico' { param($size) New-ModeIconBitmap $size '中' $black }
Write-Icon 'mode-zh-dark.ico' { param($size) New-ModeIconBitmap $size '中' $white }
Write-Icon 'mode-en-light.ico' { param($size) New-ModeIconBitmap $size '英' $black }
Write-Icon 'mode-en-dark.ico' { param($size) New-ModeIconBitmap $size '英' $white }
$black.Dispose(); $white.Dispose()
