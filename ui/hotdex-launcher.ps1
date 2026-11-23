[CmdletBinding()]
param([string]$GameDir,[switch]$ValidateOnly,[ValidateSet('Arcade','Test')][string]$Mode='Arcade',[switch]$ReturnToMenu)
$ErrorActionPreference='Stop'
try{
    . (Join-Path $PSScriptRoot 'hotdex-config.ps1')
    while($true){
        $operator=$Mode -eq 'Test'
        $info=New-HotdexStartInfo $PSScriptRoot $GameDir -Operator:$operator
        if($ValidateOnly){[pscustomobject]@{Mode=$Mode;Executable=$info.FileName;Arguments=$info.Arguments;WorkingDirectory=$info.WorkingDirectory;Settings=$info.EnvironmentVariables['LINDBERGH_SETTINGS_FILE'];GameData=$info.EnvironmentVariables['TEA_DIR'];CabinetSave=$info.EnvironmentVariables['LINDBERGH_NVRAM']}|ConvertTo-Json;exit 0}
        $nvram=$info.EnvironmentVariables['LINDBERGH_NVRAM'];$previous=Join-Path $info.WorkingDirectory 'lindbergh_nvram.bin'
        if(![IO.File]::Exists($nvram) -and [IO.File]::Exists($previous)){[IO.File]::Copy($previous,$nvram,$false)}
        $process=[Diagnostics.Process]::Start($info)
        try{
            $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$process.WaitForExit();$exitCode=$process.ExitCode
            $log=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        }finally{$process.Dispose()}
        $logName=if($operator){'hotdex-operator-last-run.log'}else{'hotdex-last-run.log'}
        $logPath=Join-Path $PSScriptRoot $logName
        [IO.File]::WriteAllText($logPath,$log,[Text.UTF8Encoding]::new($false))
        if($exitCode -eq 74){$Mode='Arcade';continue}
        if($exitCode -eq 76){$Mode='Test';continue}
        if($exitCode -eq 75 -or ($operator -and $exitCode -eq 0 -and $ReturnToMenu)){
            . (Join-Path $PSScriptRoot 'hotdex-startmenu.ps1')
            Show-HotdexStartMenu -RecompDir $PSScriptRoot
            exit 0
        }
        if($operator -and $exitCode -eq 0 -and !$ReturnToMenu){$Mode='Arcade';continue}
        if($exitCode -ne 0){throw "HOTD EX exited with code $exitCode. See $logPath"}
        exit 0
    }
}catch{
    [Console]::Error.WriteLine($_.Exception.Message)
    if(!$ValidateOnly){Add-Type -AssemblyName PresentationFramework;[void][Windows.MessageBox]::Show($_.Exception.Message,'HOTD EX - Launch error')}
    exit 1
}