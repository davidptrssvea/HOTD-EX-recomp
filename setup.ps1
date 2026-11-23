[CmdletBinding()]
param([string]$DumpPath,[string]$InstallDir,[string]$ToolchainPath,[int]$Jobs=0,[switch]$ValidateOnly,[switch]$NoDownload,[switch]$RepairTools)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
function Get-Sha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
}
$setupPhase='Package and dump validation'
$setupLocks=@();$setupLog=$null;$transcribing=$false
try {
    $logDir=Join-Path $env:LOCALAPPDATA 'HOTDEX-Builder\Logs'
    [void][IO.Directory]::CreateDirectory($logDir)
    $setupLog=Join-Path $logDir ('Setup-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)+'.log')
    Start-Transcript -LiteralPath $setupLog -Force | Out-Null;$transcribing=$true
    Write-Host "Setup log: $setupLog"
} catch { Write-Host "Setup logging could not start: $($_.Exception.Message)" }
try {
    $manifest=Get-Content -LiteralPath "$PSScriptRoot\package-manifest.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach($file in $manifest.files){
        $path=Join-Path $PSScriptRoot $file.path
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Sha256 $path) -ne $file.sha256){throw "Package integrity check failed: $($file.path). Extract a fresh copy."}
    }
    . (Join-Path $PSScriptRoot 'tools\setup_support.ps1')
    Set-BuilderPhase '1/6 Package and dump verification'
    $interactive = !$DumpPath
    if($interactive){
        Add-Type -AssemblyName System.Windows.Forms
        Write-Host 'Waiting for your dump selection in the folder dialog...'
        $picker=New-Object Windows.Forms.FolderBrowserDialog
        $picker.Description='Select your extracted HOTD EX arcade dump (disk0 or its parent folder)'
        if($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){Write-Host 'Setup cancelled.';exit 0}
        $DumpPath=$picker.SelectedPath;$picker.Dispose()
    }
    $spec=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'tools/dump-files.json') | ConvertFrom-Json
    $DumpPath=(Resolve-Path -LiteralPath $DumpPath).Path
    if([IO.File]::Exists($DumpPath)){$DumpPath=Split-Path $DumpPath -Parent}
    $candidates=@($DumpPath,(Join-Path $DumpPath 'disk0'))
    if((Split-Path $DumpPath -Leaf) -eq 'elf'){$candidates+=Split-Path $DumpPath -Parent}
    $dump=$null
    foreach($candidate in $candidates){if([IO.File]::Exists((Join-Path $candidate 'elf/hodexRI.elf'))){$dump=$candidate;break}}
    if(!$dump){throw 'Cannot find elf/hodexRI.elf. Select your extracted HOTD EX disk0 folder or its parent.'}
    foreach($file in $spec.files.PSObject.Properties){
        $path=Join-Path $dump $file.Name
        if(![IO.File]::Exists($path)){throw "Incomplete dump: missing $($file.Name)"}
        $hash=Get-Sha256 $path
        if($hash -ne $file.Value){throw "Unsupported $($file.Name) (SHA-256 $hash). Expected $($file.Value). Your original dump was left unchanged."}
    }
    foreach($name in $spec.required){
        $path=Join-Path $dump $name
        if(!(Test-Path -LiteralPath $path) -or ([IO.Directory]::Exists($path) -and !(Get-ChildItem -LiteralPath $path -Recurse -File | Select-Object -First 1))){throw "Incomplete dump: missing $name"}
    }
    Write-Host 'Verified: The House of the Dead EX (hodexRI.elf and both sound libraries).'
    if($ValidateOnly){Write-Host 'Package and dump validation passed. Nothing was installed.';exit 0}
    if(!$InstallDir){$InstallDir=Join-Path $PSScriptRoot 'HOTDEX-PC'}
    if(!$ToolchainPath){$ToolchainPath=Join-Path $env:LOCALAPPDATA 'HOTDEX-Builder\msys64'}
    $ToolchainPath=[IO.Path]::GetFullPath($ToolchainPath)
    $InstallDir=[IO.Path]::GetFullPath($InstallDir)
    Set-BuilderPhase '2/6 Host, paths and writable folders'
    Test-BuilderPreflight $PSScriptRoot $dump $InstallDir $ToolchainPath
    if($RepairTools -and $NoDownload){throw '-RepairTools needs downloads. Run without -NoDownload.'}
    $parent=Split-Path $ToolchainPath -Parent
    $setupLocks+=Enter-BuilderLock (Join-Path $PSScriptRoot '.hotdex-setup.lock')
    $setupLocks+=Enter-BuilderLock (Join-Path $parent ('.hotdex-'+(Split-Path $ToolchainPath -Leaf)+'.lock'))
    $setupLocks+=Enter-BuilderLock (Join-Path $InstallDir '.hotdex-setup.lock')
    foreach($key in @('PYTHONHOME','PYTHONPATH','CC','CXX','CFLAGS','CXXFLAGS','LDFLAGS','LIBRARY_PATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH')){
        [Environment]::SetEnvironmentVariable($key,$null,'Process')
    }
    $env:PYTHONUTF8='1';$env:PYTHONUNBUFFERED='1'
    if($RepairTools -and [IO.Directory]::Exists($ToolchainPath)){
        if(![IO.File]::Exists((Join-Path $ToolchainPath 'etc/pacman.conf')) -and ![IO.File]::Exists((Join-Path $ToolchainPath 'usr/bin/msys-2.0.dll'))){throw 'The chosen repair folder does not look like an MSYS2 cache. Select the existing MSYS2 folder.'}
        $backup=$ToolchainPath+'.backup-'+[Guid]::NewGuid().ToString('N')
        [IO.Directory]::Move($ToolchainPath,$backup)
        Write-Host "Previous tool cache preserved: $backup"
    }
    Set-BuilderPhase '3/6 Verified build tools'
    $bash=Join-Path $ToolchainPath 'usr\bin\bash.exe'
    if(!(Test-Path -LiteralPath $bash)){
        if($NoDownload){throw 'Build tools are missing. Run without -NoDownload to install them automatically.'}
        $parent=Split-Path $ToolchainPath -Parent
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        $name='msys2-base-x86_64-20260927.sfx.exe'
        $archive=Join-Path $parent $name
        $expected='AD336CCCFDA47758B5E15CDA993FBBA421115CB0B126697DAEF1EE4DFE37209F'
        Get-VerifiedBuilderArchive -Destination $archive -ExpectedHash $expected -ExpectedSize 43117824 -Urls @(
            "https://repo.msys2.org/distrib/x86_64/$name",
            "https://github.com/msys2/msys2-installer/releases/download/2026-09-27/$name")

        Write-Host 'Extracting the isolated MSYS2 build tools...'
        $stage=Join-Path $parent ('.hotdex-msys2-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $stage | Out-Null
        $extractor=Start-Process -FilePath $archive -ArgumentList @('-y', ('-o"{0}"' -f $stage)) -WindowStyle Hidden -PassThru -Wait
        $extracted=Join-Path $stage 'msys64'
        if ($extractor.ExitCode -ne 0 -or !(Test-Path -LiteralPath (Join-Path $extracted 'usr\bin\bash.exe') -PathType Leaf)) {
            throw "MSYS2 extraction failed (exit $($extractor.ExitCode)). Extraction folder: $stage"
        }

        # A previous failed extraction may have left this directory without bash.
        # Keep that directory recoverable and install the complete verified tree.
        $backup=$null
        if (Test-Path -LiteralPath $ToolchainPath) {
            if((Get-ChildItem -LiteralPath $ToolchainPath -Force | Select-Object -First 1) -and ![IO.File]::Exists((Join-Path $ToolchainPath 'etc/pacman.conf')) -and ![IO.File]::Exists((Join-Path $ToolchainPath 'usr/bin/msys-2.0.dll'))){throw 'The chosen tool-cache folder is not empty and does not look like MSYS2. Choose an empty folder; existing files were left in place.'}
            $backup=Join-Path $parent ('.hotdex-msys2-incomplete-' + [Guid]::NewGuid().ToString('N'))
            [IO.Directory]::Move((Resolve-Path -LiteralPath $ToolchainPath).Path, $backup)
        }
        try {
            [IO.Directory]::Move((Resolve-Path -LiteralPath $extracted).Path, $ToolchainPath)
        } catch {
            if ($backup) { [IO.Directory]::Move($backup, $ToolchainPath) }
            throw
        }
        [IO.Directory]::Delete($stage)
        if ($backup) { Write-Host "Previous incomplete toolchain preserved at $backup" }
    }
    $env:MSYSTEM='MINGW64';$env:CHERE_INVOKING='1';$env:MSYS2_PATH_TYPE='minimal'
    $ready=Join-Path $ToolchainPath '.hotdex-tools-ready-v1'
    $problems=@(Get-BuilderToolProblems $ToolchainPath)
    if($problems.Count){
        if($NoDownload){throw ('Cached build tools need repair: '+($problems -join ', ')+'. Run Setup without -NoDownload to repair them; use -RepairTools for a fresh tool cache if needed.')}
        Write-Host ('Installing/repairing compiler and runtime packages: '+($problems -join ', '))
        Invoke-Checked $bash @('--login','-c','pacman -Syu --noconfirm') -AllowFailure
        Invoke-Checked $bash @('--login','-c','pacman -Syu --noconfirm && pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-gcc-libs mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-python mingw-w64-x86_64-python-capstone mingw-w64-x86_64-SDL2 mingw-w64-x86_64-sdl3 mingw-w64-x86_64-glew mingw-w64-x86_64-openal mingw-w64-x86_64-zlib mingw-w64-x86_64-ffmpeg') -Attempts 3
        $problems=@(Get-BuilderToolProblems $ToolchainPath)
        if($problems.Count){throw ('The tool cache is still incomplete: '+($problems -join ', ')+'. Run Setup -RepairTools to rebuild the cache. The old cache will be preserved.')}
    }else{Write-Host 'Existing compiler, headers and Python import verified.'}
    Set-Content -LiteralPath $ready -Value '1'
    Set-BuilderPhase '4/6 Isolated Python dependencies'
    $python=Join-Path $ToolchainPath 'mingw64\bin\python.exe'
    # The pure-Python ELF reader is no longer published in MINGW64. Keep a
    # hash-verified copy in this isolated toolchain instead of changing pip
    # policy or requiring a separately installed Python.
    $pythonDeps=Join-Path $ToolchainPath 'hotdex-python'
    $pythonModules=Join-Path $pythonDeps 'pyelftools-0.33'
    $env:PYTHONPATH=$pythonModules
    $dependencyArgs=@((Join-Path $PSScriptRoot 'tools\python_deps.py'),'--cache',$pythonDeps)
    if($NoDownload){$dependencyArgs+='--no-download'}
    Invoke-Checked $python $dependencyArgs
    Invoke-Checked $python @('-c','import capstone; from elftools.elf.elffile import ELFFile')
    Write-Host 'Python build dependencies verified.'
    Set-BuilderPhase '5/6 Local generation, compilation and installation'
    Write-Host 'Generating and compiling HOTD EX locally. This can take several minutes...'
    $buildArgs = @((Join-Path $PSScriptRoot 'tools\build.py'),'--dump',$dump,'--output',$InstallDir,'--toolchain',$ToolchainPath,'--jobs',[string]$Jobs)
    Invoke-Checked $python $buildArgs
    Set-BuilderPhase '6/6 Local menu image and completion'
    $installedRoot=[IO.Path]::GetFullPath($InstallDir)
    try {
        Invoke-Checked $python @((Join-Path $PSScriptRoot 'tools\menu_background.py'),'--app',(Join-Path $installedRoot 'HOTDEX_Recomp'),'--dump',(Join-Path $installedRoot 'disk0'),'--toolchain',$ToolchainPath)
    } catch {
        Write-Warning 'The game was installed, but the optional menu background could not be rendered. See HOTDEX_Recomp\menu-background.log. Run Setup again to retry; the plain menu remains available.'
    }
    Write-Host "Ready. Open: $(Join-Path $InstallDir 'Play HOTD EX.exe')"
} catch {
    [Console]::Error.WriteLine("Setup failed during: $setupPhase")
    [Console]::Error.WriteLine($_.Exception.Message)
    if($setupLog){[Console]::Error.WriteLine("Full setup log: $setupLog")}
    exit 1
} finally {
    foreach($handle in $setupLocks){$handle.Dispose()}
    if($transcribing){Stop-Transcript | Out-Null}
}
