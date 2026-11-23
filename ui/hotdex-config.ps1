$script:HotdexDefaults=@{width=1920;height=1080;fullscreen=0;aa=4;af=16;raw_mouse=1;mouse_sens=1.0;mouse_grab=1;crosshair=1;leave_reload=1;muted=0;volume=100}
$script:HotdexAliases=@{window_w='width';window_h='height';msaa='aa';anisotropy='af';raw_input='raw_mouse';sensitivity='mouse_sens';grab='mouse_grab';reticle='crosshair'}
function Read-HotdexSettings([string]$Path) {
    $values=@{};foreach($key in $script:HotdexDefaults.Keys){$values[$key]=$script:HotdexDefaults[$key]}
    if([IO.File]::Exists($Path)){foreach($line in [IO.File]::ReadAllLines($Path)){
        if($line -notmatch '^\s*([a-zA-Z0-9_]+)\s*=\s*([^#]+)'){continue}
        $key=$Matches[1].ToLowerInvariant();$value=$Matches[2].Trim()
        if($script:HotdexAliases.ContainsKey($key)){$key=$script:HotdexAliases[$key]}
        if(!$values.ContainsKey($key)){continue}
        $number=0.0
        if([double]::TryParse($value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$number) -and ![double]::IsNaN($number) -and ![double]::IsInfinity($number)){$values[$key]=$number}
    }}
    foreach($key in 'fullscreen','raw_mouse','mouse_grab','crosshair','leave_reload','muted'){$values[$key]=[int]([int]$values[$key] -ne 0)}
    foreach($key in 'width','height'){if($values[$key] -lt 320 -or $values[$key] -gt 8192){$values[$key]=$script:HotdexDefaults[$key]};$values[$key]=[int]$values[$key]}
    if($values.aa -notin 0,2,4,8){$values.aa=0}
    if($values.aa -gt 4){$values.aa=4}
    if($values.af -eq 0){$values.af=1};if($values.af -notin 1,2,4,8,16){$values.af=1}
    $values.volume=[Math]::Max(0,[Math]::Min(100,[int]$values.volume))
    $values.mouse_sens=[Math]::Max(0.2,[Math]::Min(8.0,[double]$values.mouse_sens))
    return $values
}
function Write-HotdexSettings([string]$Path,[hashtable]$Values) {
    $lines=if([IO.File]::Exists($Path)){[IO.File]::ReadAllLines($Path)}else{@('# The House of the Dead EX - settings')}
    $written=@{};$result=[Collections.Generic.List[string]]::new()
    foreach($line in $lines){
        if($line -match '^\s*([a-zA-Z0-9_]+)\s*='){
            $key=$Matches[1].ToLowerInvariant();if($script:HotdexAliases.ContainsKey($key)){$key=$script:HotdexAliases[$key]}
            if($Values.ContainsKey($key)){
                if(!$written.ContainsKey($key)){$result.Add(('{0} = {1}' -f $key,[Convert]::ToString($Values[$key],[Globalization.CultureInfo]::InvariantCulture)));$written[$key]=$true}
                continue
            }
        }
        $result.Add($line)
    }
    foreach($key in ($Values.Keys | Sort-Object)){if(!$written.ContainsKey($key)){$result.Add(('{0} = {1}' -f $key,[Convert]::ToString($Values[$key],[Globalization.CultureInfo]::InvariantCulture)))}}
    $temporary=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try{
        [IO.File]::WriteAllText($temporary,($result -join "`r`n")+"`r`n",[Text.UTF8Encoding]::new($false))
        if([IO.File]::Exists($Path)){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}else{[IO.File]::Move($temporary,$Path)}
    }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function New-HotdexStartInfo([string]$RecompDir,[string]$GameDir) {
    if(!$GameDir){$GameDir=Join-Path (Split-Path -Parent $RecompDir) 'disk0'}
    $RecompDir=[IO.Path]::GetFullPath($RecompDir);$GameDir=[IO.Path]::GetFullPath($GameDir)
    $exe=Join-Path $RecompDir 'hotdex-desktop.exe';$elfDir=Join-Path $GameDir 'elf';$elf=Join-Path $elfDir 'hodexRI.elf';$ini=Join-Path $RecompDir 'lindbergh.ini'
    foreach($file in $exe,$elf,$ini){if(![IO.File]::Exists($file)){throw "Required file not found: $file"}}
    if(![IO.Directory]::Exists((Join-Path $GameDir 'fs'))){throw "Game data folder not found: $GameDir\fs"}
    $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.Arguments='"'+$elf+'"';$info.WorkingDirectory=$elfDir
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($key in @($info.EnvironmentVariables.Keys)){
        if($key -like 'HOTD4_*' -or $key -like 'HOTD4SP_*'){$info.EnvironmentVariables.Remove($key)}
    }
    foreach($key in 'LINDBERGH_MUTE','LINDBERGH_COUNTRY','LINDBERGH_WINDOW','LINDBERGH_WIDTH','LINDBERGH_HEIGHT','LINDBERGH_FULLSCREEN','LINDBERGH_AA','LINDBERGH_AF','LINDBERGH_RAW_MOUSE','LINDBERGH_MOUSE_SENS','LINDBERGH_MOUSE_GRAB','LINDBERGH_CROSSHAIR','LINDBERGH_GUN_CAL','LINDBERGH_STEREO'){$info.EnvironmentVariables.Remove($key)}
    $info.EnvironmentVariables['PATH']=$RecompDir+';'+$env:PATH
    $info.EnvironmentVariables['TEA_DIR']=$GameDir
    $info.EnvironmentVariables['HOTD4_SETTINGS_DIR']=$RecompDir
    $info.EnvironmentVariables['LINDBERGH_SETTINGS_FILE']=$ini
    if(!$info.EnvironmentVariables['LINDBERGH_NVRAM']){
        $info.EnvironmentVariables['LINDBERGH_NVRAM']=Join-Path $RecompDir 'lindbergh_nvram.bin'
    }
    return $info
}
