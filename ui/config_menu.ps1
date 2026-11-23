[CmdletBinding()]
param([ValidateSet('Main','Settings')][string]$Page='Main')
$ErrorActionPreference='Stop'
try {
    . (Join-Path $PSScriptRoot 'hotdex-startmenu.ps1')
    Show-HotdexStartMenu -RecompDir $PSScriptRoot -InitialPage $Page
} catch {
    Add-Type -AssemblyName PresentationFramework
    [void][Windows.MessageBox]::Show($_.Exception.GetBaseException().Message,'HOTD EX - Menu error')
    exit 1
}