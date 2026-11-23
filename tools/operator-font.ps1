param([Parameter(Mandatory=$true)][string]$OutputDir)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
[void][IO.Directory]::CreateDirectory($OutputDir)
function Save-OperatorTga([Drawing.Bitmap]$Bitmap,[string]$Name,[bool]$Font) {
    $stream=[IO.File]::Create((Join-Path $OutputDir $Name))
    $writer=New-Object IO.BinaryWriter($stream)
    try {
        $header=New-Object byte[] 18
        $header[2]=2
        [BitConverter]::GetBytes([uint16]$Bitmap.Width).CopyTo($header,12)
        [BitConverter]::GetBytes([uint16]$Bitmap.Height).CopyTo($header,14)
        $header[16]=32;$header[17]=40
        $writer.Write($header)
        for($y=0;$y -lt $Bitmap.Height;$y++) { for($x=0;$x -lt $Bitmap.Width;$x++) {
            $p=$Bitmap.GetPixel($x,$y)
            if($Font){$writer.Write([byte]255);$writer.Write([byte]255);$writer.Write([byte]255);$writer.Write([byte]$p.R)}
            else {$writer.Write([byte]$p.B);$writer.Write([byte]$p.G);$writer.Write([byte]$p.R);$writer.Write([byte]$p.A)}
        }}
    } finally {$writer.Dispose();$stream.Dispose()}
}
$bitmap=New-Object Drawing.Bitmap(256,256)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$font=New-Object Drawing.Font('Lucida Console',14,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
try {
    $graphics.Clear([Drawing.Color]::Black)
    $graphics.TextRenderingHint=[Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    for($i=32;$i -lt 127;$i++) {
        $state=$graphics.Save()
        $graphics.TranslateTransform([single](($i%16)*16),[single]([Math]::Floor($i/16)*16))
        $graphics.ScaleTransform([single]1.75,[single]1)
        $graphics.DrawString([string][char]$i,$font,[Drawing.Brushes]::White,[single]0,[single]0,[Drawing.StringFormat]::GenericTypographic)
        $graphics.Restore($state)
    }
    Save-OperatorTga $bitmap 'SEGA_KakuGothic-DB-Roman_12.tga' $true
} finally {$font.Dispose();$graphics.Dispose();$bitmap.Dispose()}
# The boot font format stores 128 glyphs starting at ASCII 32. Each record
# contains four UV floats and signed start, width and end metrics (24 bytes).
$abc=New-Object byte[] (28+128*24)
for($i=0;$i -lt 128;$i++) {
    $ch=$i+32;$offset=28+$i*24
    $u=[single](($ch%16)/16.0);$v=[single]([Math]::Floor($ch/16)/16.0)
    [BitConverter]::GetBytes($u).CopyTo($abc,$offset)
    [BitConverter]::GetBytes($v).CopyTo($abc,($offset+4))
    [BitConverter]::GetBytes([single]($u+1.0/16)).CopyTo($abc,($offset+8))
    [BitConverter]::GetBytes([single]($v+1.0/16)).CopyTo($abc,($offset+12))
    [BitConverter]::GetBytes([int16]16).CopyTo($abc,($offset+18))
    [BitConverter]::GetBytes([int16]16).CopyTo($abc,($offset+20))
}
[IO.File]::WriteAllBytes((Join-Path $OutputDir 'SEGA_KakuGothic-DB-Roman_12.abc'),$abc)
$bitmap=New-Object Drawing.Bitmap(512,64)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$font=New-Object Drawing.Font('Arial',26,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
try {
    $graphics.Clear([Drawing.Color]::Transparent)
    $graphics.DrawString('ARCADE OPERATOR SETTINGS',$font,[Drawing.Brushes]::White,[single]10,[single]10)
    Save-OperatorTga $bitmap 'logo.tga' $false
} finally {$font.Dispose();$graphics.Dispose();$bitmap.Dispose()}
Write-Output 'Generated operator menu font locally using Windows system fonts.'
