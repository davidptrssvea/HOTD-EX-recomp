[CmdletBinding()]
param([ValidateSet('Main','Settings')][string]$Page='Main',[switch]$InGame,[long]$GameWindow=0,[string]$PauseFrame)
$ErrorActionPreference='Stop'
try {
    . (Join-Path $PSScriptRoot 'hotdex-startmenu.ps1')
    $result=Show-HotdexStartMenu -RecompDir $PSScriptRoot -InitialPage $Page -InGame:$InGame -GameWindow $GameWindow -PauseFrame $PauseFrame
    if($InGame){exit ([int]$result)}
} catch {
    Add-Type -AssemblyName PresentationFramework
    [void][Windows.MessageBox]::Show($_.Exception.GetBaseException().Message,'HOTD EX - Menu error')
    exit 1
}