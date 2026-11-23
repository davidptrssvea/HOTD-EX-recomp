# Shared by the normal and Special local builders. Windows PowerShell 5.1.
function Invoke-Checked {
    param([string]$Exe,[string[]]$Arguments,[int]$Attempts=1,[switch]$AllowFailure)
    if(!(Get-Command -Name $Exe -ErrorAction SilentlyContinue)){throw "Build command is missing or cannot be found: $Exe"}
    for($attempt=1;$attempt -le $Attempts;$attempt++){
        Write-Host ('+ '+$Exe+' '+($Arguments -join ' '))
        $tail=[Collections.Generic.Queue[string]]::new()
        $previous=$ErrorActionPreference;$ErrorActionPreference='Continue'
        try{
            & $Exe @Arguments 2>&1 | ForEach-Object {
                $line=$_.ToString();Write-Host $line;$tail.Enqueue($line)
                if($tail.Count -gt 40){[void]$tail.Dequeue()}
            }
            $code=$LASTEXITCODE
        }finally{$ErrorActionPreference=$previous}
        if($code -eq 0){return}
        if($attempt -lt $Attempts){Write-Host "Command failed ($code); retrying $($attempt+1)/$Attempts...";Start-Sleep -Seconds 2}
    }
    if($AllowFailure){Write-Host "Core update returned $code; continuing in a fresh MSYS2 shell.";return}
    throw ("Command failed ($code): $Exe`n"+($tail.ToArray() -join [Environment]::NewLine))
}

function Get-VerifiedBuilderArchive {
    param([string]$Destination,[string[]]$Urls,[string]$ExpectedHash,[long]$ExpectedSize,[int]$DelaySeconds=2)
    if($ExpectedHash -notmatch '^[0-9a-fA-F]{64}$' -or $ExpectedSize -le 0){throw 'Invalid build-tools integrity metadata.'}
    if([IO.File]::Exists($Destination) -and ([IO.FileInfo]$Destination).Length -eq $ExpectedSize -and (Get-Sha256 $Destination) -eq $ExpectedHash){
        Write-Host 'Reusing the verified build-tools archive.';return
    }
    $partial=$Destination+'.partial';$details='No download completed.'
    for($attempt=0;$attempt -lt 3;$attempt++){
        $url=$Urls[$attempt % $Urls.Count]
        if($attempt -eq 2){$url+='?hotdex_retry='+[Guid]::NewGuid().ToString('N')}
        try{
            Write-Host "Downloading build tools (attempt $($attempt+1)/3): $url"
            Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $partial -TimeoutSec 120 -Headers @{'Cache-Control'='no-cache'}
            $size=([IO.FileInfo]$partial).Length;$hash=Get-Sha256 $partial
            $details="Received $size bytes; SHA-256 $hash; URL $url."
            if($size -ne $ExpectedSize -or $hash -ne $ExpectedHash){throw $details}
            if([IO.File]::Exists($Destination)){[IO.File]::Replace($partial,$Destination,[NullString]::Value)}else{[IO.File]::Move($partial,$Destination)}
            Write-Host 'Build-tools size and SHA-256 verified.';return
        }catch{
            $details=$_.Exception.Message;Write-Host "Download attempt failed: $details"
            if($attempt -lt 2 -and $DelaySeconds -gt 0){Start-Sleep -Seconds ($DelaySeconds*($attempt+1))}
        }
    }
    throw "Build-tools download could not be verified after 3 attempts. Expected $ExpectedSize bytes and SHA-256 $ExpectedHash. $details Download file: $partial"
}

function Enter-BuilderLock {
    param([string]$Path)
    try{return [IO.File]::Open($Path,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    catch{throw "Another installer may be using this builder or tool cache, or the folder is not writable. Close the other Setup and try again. Lock: $Path. $($_.Exception.Message)"}
}

function Test-BuilderWritable {
    param([string]$Directory)
    $probe=Join-Path $Directory ('.hotdex-write-'+[Guid]::NewGuid().ToString('N')+'.tmp')
    try{[void][IO.Directory]::CreateDirectory($Directory);[IO.File]::WriteAllText($probe,'write test')}
    catch{throw "Setup cannot write to $Directory. Choose a writable folder. $($_.Exception.Message)"}
    finally{if([IO.File]::Exists($probe)){[IO.File]::Delete($probe)}}
}

function Test-BuilderPreflight {
    param([string]$Root,[string]$Dump,[string]$Output,[string]$Toolchain)
    if($PSVersionTable.PSVersion -lt [version]'5.1' -or ![Environment]::Is64BitProcess){throw 'Use 64-bit Windows PowerShell 5.1 on Windows 10/11 x64. Start Setup.cmd rather than a 32-bit PowerShell host.'}
    $compiler=Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    if(![IO.File]::Exists($compiler)){throw "The Windows .NET Framework 4.x C# compiler is missing: $compiler. Repair/install .NET Framework before running Setup."}
    $outputFull=[IO.Path]::GetFullPath($Output).TrimEnd('\','/')
    $toolsFull=[IO.Path]::GetFullPath($Toolchain).TrimEnd('\','/')
    foreach($protected in @($Root,$Dump,$outputFull)){
        $p=[IO.Path]::GetFullPath($protected).TrimEnd('\','/')
        if($p.Equals($toolsFull,[StringComparison]::OrdinalIgnoreCase) -or $p.StartsWith($toolsFull+'\',[StringComparison]::OrdinalIgnoreCase) -or $toolsFull.StartsWith($p+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw 'The MSYS2 cache must be separate from your dump, builder and installation root.'
        }
    }
    foreach($protected in @($Dump,(Join-Path $Root 'source'),(Join-Path $Root 'tools'),(Join-Path $Root 'ui'))){
        $p=[IO.Path]::GetFullPath($protected).TrimEnd('\','/')
        if($p.Equals($outputFull,[StringComparison]::OrdinalIgnoreCase) -or $p.StartsWith($outputFull+'\',[StringComparison]::OrdinalIgnoreCase) -or $outputFull.StartsWith($p+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw 'Choose an installation folder outside your original dump and the builder source/tools/UI folders.'
        }
    }
    foreach($directory in @($Root,$outputFull,(Split-Path $Toolchain -Parent))){
        if($directory.Contains(';')){throw "Choose a folder without a semicolon (CMake uses it as a list separator): $directory"}
        Test-BuilderWritable $directory
        $drive=[IO.Path]::GetPathRoot([IO.Path]::GetFullPath($directory))
        if($drive -notlike '\\*'){
            $free=[IO.DriveInfo]::new($drive).AvailableFreeSpace
            Write-Host ('Available space on {0}: {1:N1} GB' -f $drive,($free/1GB))
            if($free -lt 256MB){throw "Not enough free space on $drive. Free space or select folders on another drive before building."}
        }
    }
    if(([IO.Path]::GetFullPath($Root).Length+105) -gt 255){throw 'The builder path is too long for the Windows build tools. Extract it into a shorter folder, for example D:\HOTDEX-Builder.'}
    if(([IO.Path]::GetFullPath($Toolchain).Length+80) -gt 255){throw 'The MSYS2 tool-cache path is too long. Use -ToolchainPath with a shorter writable local path.'}
    # Reserve by volume, so placing all three folders on one disk cannot
    # accidentally count the same free space three times.
    [long]$copyBytes=256MB
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $Dump 'fs') -Recurse -File){
        $relative=$file.FullName.Substring((Join-Path $Dump 'fs').Length).TrimStart('\','/')
        $destination=Join-Path (Join-Path $Output 'disk0/fs') $relative
        [long]$existing=if([IO.File]::Exists($destination)){([IO.FileInfo]$destination).Length}else{0}
        $copyBytes+=[Math]::Max([long]0,[long]$file.Length-$existing)
    }
    $buildReserve=if([IO.Directory]::Exists((Join-Path $Root 'build'))){512MB}else{3GB}
    $toolReserve=if([IO.File]::Exists((Join-Path $Toolchain 'mingw64/bin/gcc.exe'))){512MB}else{4GB}
    $budget=@{}
    foreach($entry in @(@($Root,$buildReserve),@($Output,$copyBytes),@((Split-Path $Toolchain -Parent),$toolReserve))){
        $volume=[IO.Path]::GetPathRoot([IO.Path]::GetFullPath($entry[0]))
        if($volume -like '\\*'){continue}
        if(!$budget.ContainsKey($volume)){$budget[$volume]=[long]0}
        $budget[$volume]+=[long]$entry[1]
    }
    foreach($volume in $budget.Keys){
        [long]$free=[IO.DriveInfo]::new($volume).AvailableFreeSpace
        Write-Host ('Estimated additional space on {0}: {1:N1} GB' -f $volume,($budget[$volume]/1GB))
        if($free -lt $budget[$volume]){throw "Insufficient free space on $volume for the build tools, compilation and game copy. Free space or select folders on another drive."}
    }
}

function Get-BuilderToolProblems {
    param([string]$Toolchain)
    $missing=@()
    foreach($relative in @('mingw64/bin/ffmpeg.exe','mingw64/bin/gcc.exe','mingw64/bin/cmake.exe','mingw64/bin/ninja.exe','mingw64/bin/python.exe','mingw64/bin/objdump.exe','mingw64/bin/strip.exe',
                           'mingw64/include/GL/glew.h','mingw64/include/SDL2/SDL.h','mingw64/include/AL/al.h','mingw64/lib/cmake/SDL2/SDL2Config.cmake')){
        if(![IO.File]::Exists((Join-Path $Toolchain $relative))){$missing+=$relative}
    }
    if($missing.Count){return $missing}
    $sdl2=Join-Path $Toolchain 'mingw64/bin/SDL2.dll'
    if(![IO.File]::Exists($sdl2)){$missing+='mingw64/bin/SDL2.dll'}
    elseif([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($sdl2)).Contains('SDL3.dll') -and ![IO.File]::Exists((Join-Path $Toolchain 'mingw64/bin/SDL3.dll'))){$missing+='mingw64/bin/SDL3.dll (needed by sdl2-compat)'}
    if($missing.Count){return $missing}
    $oldPath=$env:PATH;$env:PATH=(Join-Path $Toolchain 'mingw64/bin')+[IO.Path]::PathSeparator+$env:PATH
    $previous=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{
        foreach($probe in @(@('gcc.exe','--version'),@('cmake.exe','--version'),@('ninja.exe','--version'),@('ffmpeg.exe','-version'),@('python.exe','-c','import capstone'))){
            $exe=Join-Path $Toolchain ('mingw64/bin/'+$probe[0]);$probeArgs=@($probe | Select-Object -Skip 1)
            $text=& $exe @probeArgs 2>&1
            if($LASTEXITCODE -ne 0){$missing+=($probe[0]+' cannot run: '+($text -join ' '))}
        }
    }finally{$ErrorActionPreference=$previous;$env:PATH=$oldPath}
    return $missing
}

function Set-BuilderPhase {
    param([string]$Name)
    $script:setupPhase=$Name
    Write-Host "`n=== $Name ==="
}
