<#
.SYNOPSIS
  產生輸入法用的圖示，放在 windows\tip：
  - BangbangZhuyin.ico：天空藍底深藍字「棒」（UD 教科書體，鍵盤清單、語言列、工作列）。對應 Mac 版的 scripts/make-icon.swift。
    加上 -PreviewSheet <PNG 檔> 時，只畫出「字體 × 底色」的選項比較圖，不產生圖示。
  - mode-zh-light.ico、mode-zh-dark.ico、mode-en-light.ico、mode-en-dark.ico：工作列的「中／英」輸入模式圖示，
    透明背景；light 是淺色工作列用的黑字，dark 是深色工作列用的白字。
  每個圖示都有 16、20、24、32、40、48 像素。圖示已經放在 repo 裡，只有要改圖示時才需要重新執行。

.EXAMPLE
  windows\scripts\make-icon.ps1
#>
param(
    [string] $OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'tip'),
    # 只畫出「字體 × 底色」的選項比較圖（PNG），不產生圖示。
    [string] $PreviewSheet
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

function Write-Glyph($g, [int] $size, [string] $glyph, [single] $scale, [System.Drawing.FontStyle] $style, [System.Drawing.Brush] $brush, [single] $offsetY, [string] $family = 'Microsoft JhengHei UI') {
    $font = New-Object System.Drawing.Font($family, [single]($size * $scale), $style, [System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF(0, [single]($size * $offsetY), [single]$size, [single]$size)
    $g.DrawString($glyph, $font, $brush, $rect, $format)
    $font.Dispose(); $format.Dispose()
}

# 輸入法圖示的樣式：字體（含大小比例、粗體、上下微調）與底色、字色。
# 2026-11-23 使用者從 -PreviewSheet 的選項裡挑了 5E：UD 教科書體粗體、天空藍底深藍字（原本是藍底白字的微軟正黑體粗體）。
$appFont = @{ Name = 'UD 教科書體 粗體'; Family = 'UD Digi Kyokasho NK'; Scale = 0.74; Bold = $true; OffsetY = 0.03 }
$appColors = @{ Name = '天空藍'; Background = '#BFDDF5'; Foreground = '#2D5F8A' }

function ConvertTo-Color([string] $hex) {
    $v = [Convert]::ToInt32($hex.TrimStart('#'), 16)
    return [System.Drawing.Color]::FromArgb(255, ($v -shr 16) -band 255, ($v -shr 8) -band 255, $v -band 255)
}

# 圓角方塊底上一個「棒」字。
function New-AppIconBitmap([int] $size, $font = $appFont, $colors = $appColors) {
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
    $background = New-Object System.Drawing.SolidBrush (ConvertTo-Color $colors.Background)
    $foreground = New-Object System.Drawing.SolidBrush (ConvertTo-Color $colors.Foreground)
    $g.FillPath($background, $path)
    $style = if ($font.Bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    Write-Glyph $g $size '棒' $font.Scale $style $foreground $font.OffsetY $font.Family
    $background.Dispose(); $foreground.Dispose(); $path.Dispose(); $g.Dispose()
    return $bitmap
}

# 選項比較圖：每一列是一種字體，每一欄是一種底色；每格是 64 像素的放大圖，以及 32、16 像素的實際大小
# （顯示器 200%、100% 時工作列上的大小），底色是淺色工作列的顏色。
function Write-PreviewSheet([string] $path) {
    $fonts = @(
        @{ Name = '1 微軟正黑體 粗體（現在的）'; Family = 'Microsoft JhengHei UI'; Scale = 0.72; Bold = $true; OffsetY = 0.03 },
        @{ Name = '2 思源黑體 Black'; Family = 'Noto Sans TC Black'; Scale = 0.70; Bold = $false; OffsetY = 0.02 },
        @{ Name = '3 思源宋體 Black（明體）'; Family = 'Noto Serif TC Black'; Scale = 0.70; Bold = $false; OffsetY = 0.02 },
        @{ Name = '4 標楷體 粗體'; Family = 'DFKai-SB'; Scale = 0.80; Bold = $true; OffsetY = 0.04 },
        @{ Name = '5 UD 教科書體 粗體'; Family = 'UD Digi Kyokasho NK'; Scale = 0.74; Bold = $true; OffsetY = 0.03 }
    )
    $colors = @(
        @{ Name = 'A 草莓粉'; Background = '#F8C8D4'; Foreground = '#8E3A55' },
        @{ Name = 'B 薄荷綠'; Background = '#BEE5D3'; Foreground = '#2F6B55' },
        @{ Name = 'C 薰衣草紫'; Background = '#D9CCF3'; Foreground = '#5B4699' },
        @{ Name = 'D 檸檬黃'; Background = '#FBE7A1'; Foreground = '#7A5C12' },
        @{ Name = 'E 天空藍'; Background = '#BFDDF5'; Foreground = '#2D5F8A' }
    )
    $labelWidth = 250; $cellWidth = 170; $cellHeight = 96; $top = 60
    $sheet = New-Object System.Drawing.Bitmap ($labelWidth + $cellWidth * $colors.Count + 10), ($top + $cellHeight * $fonts.Count + 10)
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::White)
    $label = New-Object System.Drawing.Font('Microsoft JhengHei UI', 15, [System.Drawing.GraphicsUnit]::Pixel)
    $small = New-Object System.Drawing.Font('Segoe UI', 12, [System.Drawing.GraphicsUnit]::Pixel)
    $ink = [System.Drawing.Brushes]::Black
    $taskbar = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 243, 243, 243))
    for ($c = 0; $c -lt $colors.Count; $c++) {
        $x = $labelWidth + $c * $cellWidth
        $g.DrawString($colors[$c].Name, $label, $ink, $x + 8, 8)
        $g.DrawString("$($colors[$c].Background) / $($colors[$c].Foreground)", $small, [System.Drawing.Brushes]::DimGray, $x + 8, 32)
    }
    for ($f = 0; $f -lt $fonts.Count; $f++) {
        $y = $top + $f * $cellHeight
        $g.DrawString($fonts[$f].Name, $label, $ink, 10, $y + 36)
        for ($c = 0; $c -lt $colors.Count; $c++) {
            $x = $labelWidth + $c * $cellWidth
            $g.FillRectangle($taskbar, $x + 4, $y + 4, $cellWidth - 8, $cellHeight - 8)
            foreach ($item in @(@{ Size = 64; X = 14; Y = 16 }, @{ Size = 32; X = 96; Y = 32 }, @{ Size = 16; X = 140; Y = 40 })) {
                $icon = New-AppIconBitmap $item.Size $fonts[$f] $colors[$c]
                $g.DrawImage($icon, $x + $item.X, $y + $item.Y, $item.Size, $item.Size)
                $icon.Dispose()
            }
        }
    }
    $sheet.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $label.Dispose(); $small.Dispose(); $taskbar.Dispose(); $g.Dispose(); $sheet.Dispose()
    Write-Host "已產生選項比較圖 $path"
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

if ($PreviewSheet) {
    Write-PreviewSheet $PreviewSheet
    return
}

$black = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
Write-Icon 'BangbangZhuyin.ico' { param($size) New-AppIconBitmap $size }
Write-Icon 'mode-zh-light.ico' { param($size) New-ModeIconBitmap $size '中' $black }
Write-Icon 'mode-zh-dark.ico' { param($size) New-ModeIconBitmap $size '中' $white }
Write-Icon 'mode-en-light.ico' { param($size) New-ModeIconBitmap $size '英' $black }
Write-Icon 'mode-en-dark.ico' { param($size) New-ModeIconBitmap $size '英' $white }
$black.Dispose(); $white.Dispose()
