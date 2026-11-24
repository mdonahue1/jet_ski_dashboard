param(
  [Parameter(Mandatory = $true)][string]$InputPng,
  [string]$OutputC = "Core/Src/splash_image.c",
  [string]$PreviewPng = "splash_preview.png",
  [string]$FunctionName = "SplashImage_Draw",
  [string]$SymbolPrefix = "splash",
  [ValidateSet("White", "Black")][string]$Background = "White",
  [int]$CropTop = 3
)

Add-Type -AssemblyName System.Drawing

$screenW = 480
$screenH = 320
$source = [System.Drawing.Bitmap]::new($InputPng)
$canvas = [System.Drawing.Bitmap]::new($screenW, $screenH, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$graphics = [System.Drawing.Graphics]::FromImage($canvas)
$graphics.Clear($(if ($Background -eq "Black") { [System.Drawing.Color]::Black } else { [System.Drawing.Color]::White }))
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

# Drop the screenshot's thin top-edge artifact, then preserve the complete logo.
$cropTop = [Math]::Max(0, [Math]::Min($CropTop, $source.Height - 1))
$srcRect = [System.Drawing.Rectangle]::new(0, $cropTop, $source.Width, $source.Height - $cropTop)
$scale = [Math]::Min($screenW / $srcRect.Width, $screenH / $srcRect.Height)
$drawW = [int][Math]::Round($srcRect.Width * $scale)
$drawH = [int][Math]::Round($srcRect.Height * $scale)
$drawX = [int](($screenW - $drawW) / 2)
$drawY = [int](($screenH - $drawH) / 2)
$dstRect = [System.Drawing.Rectangle]::new($drawX, $drawY, $drawW, $drawH)
$graphics.DrawImage($source, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
$graphics.Dispose()
$source.Dispose()

# Sixteen grayscale levels retain antialiasing while making RLE very compact.
$indices = [byte[]]::new($screenW * $screenH)
$preview = [System.Drawing.Bitmap]::new($screenW, $screenH, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$pos = 0
for ($y = 0; $y -lt $screenH; $y++) {
  for ($x = 0; $x -lt $screenW; $x++) {
    $pixel = $canvas.GetPixel($x, $y)
    $luma = [int][Math]::Round((0.299 * $pixel.R) + (0.587 * $pixel.G) + (0.114 * $pixel.B))
    $index = [int][Math]::Round($luma * 15.0 / 255.0)
    $indices[$pos++] = [byte]$index
    $gray = [int][Math]::Round($index * 255.0 / 15.0)
    $preview.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($gray, $gray, $gray))
  }
}
$canvas.Dispose()
$preview.Save($PreviewPng, [System.Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()

# Token: high 4 bits = palette index, low 12 bits = run length minus one.
$tokens = [System.Collections.Generic.List[uint16]]::new()
$runIndex = [int]$indices[0]
$runLength = 1
for ($i = 1; $i -lt $indices.Length; $i++) {
  $next = [int]$indices[$i]
  if (($next -eq $runIndex) -and ($runLength -lt 4096)) {
    $runLength++
  } else {
    $tokens.Add([uint16](($runIndex -shl 12) -bor ($runLength - 1)))
    $runIndex = $next
    $runLength = 1
  }
}
$tokens.Add([uint16](($runIndex -shl 12) -bor ($runLength - 1)))

$builder = [System.Text.StringBuilder]::new()
[void]$builder.AppendLine('#include "splash_image.h"')
[void]$builder.AppendLine('#include "st7796.h"')
[void]$builder.AppendLine('')
[void]$builder.AppendLine("static const uint16_t ${SymbolPrefix}_palette[16] = {")
for ($i = 0; $i -lt 16; $i++) {
  $gray = [int][Math]::Round($i * 255.0 / 15.0)
  $rgb565 = (($gray -shr 3) -shl 11) -bor (($gray -shr 2) -shl 5) -bor ($gray -shr 3)
  [void]$builder.Append(('  0x{0:X4}U{1}' -f $rgb565, $(if ($i -lt 15) { ',' } else { '' })))
  [void]$builder.AppendLine()
}
[void]$builder.AppendLine('};')
[void]$builder.AppendLine('')
[void]$builder.AppendLine("static const uint16_t ${SymbolPrefix}_tokens[] = {")
for ($i = 0; $i -lt $tokens.Count; $i++) {
  if (($i % 12) -eq 0) { [void]$builder.Append('  ') }
  [void]$builder.Append(('0x{0:X4}U' -f $tokens[$i]))
  if ($i -lt ($tokens.Count - 1)) { [void]$builder.Append(', ') }
  if ((($i % 12) -eq 11) -or ($i -eq ($tokens.Count - 1))) { [void]$builder.AppendLine() }
}
[void]$builder.AppendLine('};')
[void]$builder.AppendLine('')
[void]$builder.AppendLine("void ${FunctionName}(void)")
[void]$builder.AppendLine('{')
[void]$builder.AppendLine('  ST7796_DrawIndexedRle(0U, 0U, 480U, 320U,')
[void]$builder.AppendLine("                          ${SymbolPrefix}_tokens,")
[void]$builder.AppendLine("                          (uint32_t)(sizeof(${SymbolPrefix}_tokens) / sizeof(${SymbolPrefix}_tokens[0])),")
[void]$builder.AppendLine("                          ${SymbolPrefix}_palette);")
[void]$builder.AppendLine('}')

$absoluteOutput = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputC))
[System.IO.File]::WriteAllText($absoluteOutput, $builder.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Output "Generated $($tokens.Count) RLE tokens ($($tokens.Count * 2) bytes)"
