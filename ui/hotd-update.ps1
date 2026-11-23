param([string]$UpdateRequest)

function Get-HotdUpdateGame([string]$Game) {
    switch ($Game) {
        Normal  { @{ Repo='HOTD4-recomp'; Bundle='HOTD4-Local-Builder'; App='HOTD4_Recomp'; Exe='hotd4-desktop.exe'; VR='hotd4-vr.exe'; Launcher='Play HOTD4.exe'; Dumps=@('game/disk0','The House of the Dead 4/disk0'); Elf='elf/hod4M.elf'; Lock='.hotd4-setup.lock'; RootExe=@('Play HOTD4.exe','Model Viewer.exe') } }
        Special { @{ Repo='HOTD-4-Special-recomp'; Bundle='HOTD4SP-Local-Builder'; App='HOTD4SP_Recomp'; Exe='hotd4sp-desktop.exe'; VR='hotd4sp-vr.exe'; Launcher='HOTD4SP_Recomp/Play HOTD4 Special.exe'; Dumps=@('hotd4sp'); Elf='elf/hod4M.elf'; Lock='.hotd4-setup.lock'; RootExe=@() } }
        EX      { @{ Repo='HOTD-EX-recomp'; Bundle='HOTDEX-Local-Builder'; App='HOTDEX_Recomp'; Exe='hotdex-desktop.exe'; Launcher='Play HOTD EX.exe'; Dumps=@('disk0'); Elf='elf/hodexRI.elf'; Lock='.hotdex-setup.lock'; RootExe=@('Play HOTD EX.exe') } }
        default { throw 'Unknown HOTD edition.' }
    }
}

function ConvertTo-HotdVersion([string]$Tag) {
    if ($Tag -match '^v\d{1,5}\.\d{1,5}\.\d{1,5}$') { [version]$Tag.Substring(1) }
}

function Get-HotdReleasePackage($Game, $Release) {
    if ($Release.tag_name -notmatch '^v\d{1,5}\.\d{1,5}\.\d{1,5}$' -or $Release.draft -or $Release.prerelease) {
        throw 'GitHub did not return a stable release.'
    }
    $name = "$($Game.Bundle)-$($Release.tag_name).zip"
    $assets = @($Release.assets | Where-Object name -CEQ $name)
    $url = "https://github.com/davidptrssvea/$($Game.Repo)/releases/download/$($Release.tag_name)/$name"
    if ($assets.Count -ne 1 -or $assets[0].browser_download_url -cne $url -or
        $assets[0].digest -notmatch '^sha256:[0-9a-f]{64}$' -or $assets[0].size -le 0 -or $assets[0].size -gt 50MB) {
        throw 'The release has no verifiable installer ZIP.'
    }
    @{ Tag=[string]$Release.tag_name; Url=$url; Hash=$assets[0].digest.Substring(7); Size=[long]$assets[0].size }
}

function Expand-HotdUpdateBundle([string]$Zip, [string]$Directory, [string]$Bundle) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $total = 0L
    try {
        if ($archive.Entries.Count -gt 10000) { throw 'Too many files in the installer ZIP.' }
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName
            $parts = $name.TrimEnd('/').Split('/')
            if ($parts[0] -cne $Bundle -or $name -match '[\\:]' -or
                @($parts | Where-Object { !$_.Length -or $_ -match '[. ]$' -or $_ -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)' }).Count) {
                throw "Invalid path in installer ZIP: $name"
            }
            if (!$entry.Name) { continue }
            if ($parts.Count -lt 2 -or !$seen.Add($name)) { throw "Duplicate or invalid ZIP entry: $name" }
            $total += $entry.Length
            if ($total -gt 200MB) { throw 'The installer ZIP expands beyond the supported limit.' }
            $target = Join-Path $Directory $name
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target)
        }
    } finally { $archive.Dispose() }
    $root = Join-Path $Directory $Bundle
    if (!(Test-Path -LiteralPath (Join-Path $root 'setup.ps1') -PathType Leaf)) { throw 'Setup is missing from the ZIP.' }
    $root
}

function Get-HotdManagedPaths([string]$Stage, [string]$Builder, $Game) {
    $prefix = $Game.App + '/'
    $manifest = [IO.File]::ReadAllText((Join-Path $Builder 'package-manifest.json')) | ConvertFrom-Json
    foreach ($item in $manifest.files) {
        if ($item.path -notmatch '^ui/((?:[\w .()/-]+))$') { continue }
        $relative = $Matches[1]
        if ($relative -match '(^|/)\.\.?(/|$)') { throw 'Invalid managed path in manifest.' }
        if ([IO.Path]::GetExtension($relative) -notin '.ps1','.psm1','.cs','.xaml','.cmd','.bat','.md' -and
            $relative -notmatch '^(achievement-assets|crosshair-previews)/') { continue }
        if ([IO.File]::Exists((Join-Path $Stage ($prefix+$relative)))) { $prefix+$relative }
        elseif ($relative -match '^[^/]+\.cmd$' -and [IO.File]::Exists((Join-Path $Stage $relative))) { $relative }
    }
    Get-ChildItem -LiteralPath (Join-Path $Stage $Game.App) -File |
        Where-Object Extension -in '.exe','.dll' | ForEach-Object { $prefix+$_.Name }
    $stagedApp = Join-Path $Stage $Game.App
    foreach ($name in 'operator-assets','licenses') {
        $directory = Join-Path $stagedApp $name
        if ([IO.Directory]::Exists($directory)) {
            Get-ChildItem -LiteralPath $directory -Recurse -File |
                ForEach-Object { (($_.FullName.Substring($Stage.Length+1)) -replace '\\','/') }
        }
    }
    foreach ($name in @($Game.RootExe) + @('Play HOTD EX.cmd','Play HOTD EX (Direct).cmd','Settings.cmd','Play HOTD Special.cmd','local-build.json')) {
        if ([IO.File]::Exists((Join-Path $Stage $name))) { $name }
    }
    $prefix+'hotd-update-version.txt'
}

function Install-HotdManagedFiles([string]$Stage, [string]$Root, [string]$Backup, [string[]]$Paths) {
    $applied = [Collections.Generic.List[object]]::new()
    try {
        foreach ($relative in $Paths) {
            $target = Join-Path $Root $relative
            $saved = Join-Path $Backup $relative
            $source = Join-Path $Stage $relative
            $directory = [IO.Path]::GetDirectoryName($target)
            for ($parent = $directory; $parent -and $parent -ne $Root; $parent = [IO.Path]::GetDirectoryName($parent)) {
                if ([IO.Directory]::Exists($parent) -and ([IO.File]::GetAttributes($parent) -band [IO.FileAttributes]::ReparsePoint)) {
                    throw "Move the linked installation folder before updating: $parent"
                }
            }
            [void][IO.Directory]::CreateDirectory($directory)
            $existing = [IO.File]::Exists($target)
            if ($existing) {
                if ([IO.File]::GetAttributes($target) -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Move the linked program file before updating: $target"
                }
                [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($saved))
                [IO.File]::Replace($source, $target, $saved)
            } else { [IO.File]::Move($source, $target) }
            $applied.Add(@{ Target=$target; Saved=$saved; Existing=$existing })
        }
    } catch {
        $failure = $_
        $rollback = @()
        for ($i = $applied.Count-1; $i -ge 0; $i--) {
            $item = $applied[$i]
            try {
                if ($item.Existing) {
                    if ([IO.File]::Exists($item.Target)) {
                        $failed = $item.Saved + '.failed'
                        [IO.File]::Replace($item.Saved, $item.Target, $failed)
                        [IO.File]::Delete($failed)
                    }
                    else { [IO.File]::Move($item.Saved, $item.Target) }
                } else { [IO.File]::Delete($item.Target) }
            } catch { $rollback += $_.Exception.Message }
        }
        if ($rollback.Count) { throw "Update failed: $failure. Backup: $Backup. Restore errors: $($rollback -join ', ')" }
        throw $failure
    }
}

function Get-HotdPowerShell {
    Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell/v1.0/powershell.exe'
}

function Invoke-HotdUpdateWorker([string]$Request) {
    $ErrorActionPreference = 'Stop'
    $job = [IO.File]::ReadAllText($Request) | ConvertFrom-Json
    $work = [IO.Path]::GetDirectoryName($Request)
    $status = Join-Path $work 'status.txt'
    $game = Get-HotdUpdateGame $job.Game
    $app = [IO.Path]::GetFullPath($job.App)
    $root = [IO.Path]::GetDirectoryName($app)
    $stage = Join-Path $work 'install'
    $ready = $false
    $lock = $null
    $owner = [Diagnostics.Process]::GetProcessById([int]$job.Owner)
    try {
        if ([IO.Path]::GetFileName($app) -ne $game.App -or !(Test-Path -LiteralPath (Join-Path $app $game.Exe) -PathType Leaf)) { throw 'The current installation could not be located.' }
        $lock = [IO.File]::Open((Join-Path $root $game.Lock), 'OpenOrCreate', 'ReadWrite', 'None')
        $installed = ConvertTo-HotdVersion ([IO.File]::ReadAllText((Join-Path $app 'hotd-update-version.txt')).Trim())
        $tag = $job.Package.Tag
        $candidate = ConvertTo-HotdVersion $tag
        $url = "https://github.com/davidptrssvea/$($game.Repo)/releases/download/$tag/$($game.Bundle)-$tag.zip"
        if (!$candidate -or !$installed -or $candidate -le $installed -or $job.Package.Url -cne $url -or
            $job.Package.Hash -notmatch '^[0-9a-f]{64}$' -or $job.Package.Size -le 0 -or $job.Package.Size -gt 50MB) {
            throw 'This update does not match the installed edition and version.'
        }
        $dump = $game.Dumps | ForEach-Object { Join-Path $root $_ } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ $game.Elf) -PathType Leaf } | Select-Object -First 1
        if (!$dump) { throw 'The installed arcade dump could not be located.' }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $zip = Join-Path $work 'builder.zip'
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 90 -Uri $url -OutFile $zip
        if ((Get-Item -LiteralPath $zip).Length -ne $job.Package.Size -or
            (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $job.Package.Hash) { throw 'Installer ZIP verification failed.' }
        $builder = Expand-HotdUpdateBundle $zip $work $game.Bundle
        $setup = Join-Path $builder 'setup.ps1'
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = Get-HotdPowerShell
        $info.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -DumpPath "{1}" -InstallDir "{2}" -StageUpdate' -f $setup,$dump,$stage
        $buildOpenXR = $game.VR -and [IO.File]::Exists((Join-Path $app $game.VR))
        if ($buildOpenXR) { $info.Arguments += ' -OpenXR' }
        $info.WorkingDirectory = $builder
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $info.RedirectStandardError = $true
        [IO.File]::WriteAllText($status, 'Building the update locally. This can take several minutes.')
        $builderProcess = [Diagnostics.Process]::Start($info)
        try {
            $builderProcess.PriorityClass = 'BelowNormal'
            $stdout = $builderProcess.StandardOutput.ReadToEndAsync()
            $stderr = $builderProcess.StandardError.ReadToEndAsync()
            $builderProcess.WaitForExit()
            [IO.File]::WriteAllText((Join-Path $work 'setup.log'), $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult())
            if ($builderProcess.ExitCode) { throw 'Setup could not build the update. See setup.log.' }
        } finally { $builderProcess.Dispose() }
        $builtApp = Join-Path $stage $game.App
        if (!(Test-Path -LiteralPath (Join-Path $builtApp $game.Exe) -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $stage $game.Launcher) -PathType Leaf) -or
            ($buildOpenXR -and ![IO.File]::Exists((Join-Path $builtApp $game.VR))) -or
            !(Test-Path -LiteralPath (Join-Path $builtApp 'hotd-update.ps1') -PathType Leaf)) {
            throw 'The staged update is incomplete.'
        }
        [IO.File]::WriteAllText((Join-Path $builtApp 'hotd-update-version.txt'), $job.Package.Tag)
        $files = @(Get-HotdManagedPaths $stage $builder $game | Select-Object -Unique)
        [IO.File]::WriteAllText($status, 'Ready')
        $ready = $true
        if (!$owner.WaitForExit(60000)) { throw 'Close the game menu and retry the update.' }
        $gameProcesses = @($game.Exe, $game.VR) | Where-Object { $_ } |
            ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) }
        $running = Get-Process -Name $gameProcesses -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $app }
        if ($running) { throw 'Close the running game before updating.' }
        Install-HotdManagedFiles $stage $root (Join-Path $work 'backup') $files
        [IO.File]::WriteAllText($status, "Installed $($job.Package.Tag)")
        try {
            foreach ($name in @($game.Bundle, 'install')) {
                $folder = Join-Path $work $name
                $resolved = (Resolve-Path -LiteralPath $folder -ErrorAction SilentlyContinue).ProviderPath
                if ($resolved -and [IO.Path]::GetDirectoryName($resolved) -eq $work -and
                    !([IO.File]::GetAttributes($resolved) -band [IO.FileAttributes]::ReparsePoint)) {
                    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
            [IO.File]::Delete($zip)
        } catch { Write-Verbose "The update is installed. Temporary files remain in $work." }
    } catch {
        $message = $_.Exception.Message + "`n`nUpdate files and logs: $work"
        [IO.File]::WriteAllText($status, 'Error: '+$message)
        if ($ready) {
            Add-Type -AssemblyName PresentationFramework
            [void][Windows.MessageBox]::Show($message, 'HOTD update', 'OK', 'Error')
        }
    } finally {
        if ($lock) { $lock.Dispose() }
        $owner.Dispose()
    }
    if ($ready -and [IO.File]::Exists((Join-Path $root $game.Launcher))) {
        Start-Process -FilePath (Join-Path $root $game.Launcher) -WorkingDirectory $root
    }
}

function Add-HotdUpdater($Window, $MainPage, [string]$App, [string]$Edition) {
    $game = Get-HotdUpdateGame $Edition
    $versionFile = Join-Path $App 'hotd-update-version.txt'
    $tag = if ([IO.File]::Exists($versionFile)) { [IO.File]::ReadAllText($versionFile).Trim() }
    $installed = ConvertTo-HotdVersion $tag
    $button = [Windows.Controls.Button]::new()
    $button.Name = 'Updates'
    $button.Content = if ($installed) { "Updates ($tag)" } else { 'Updates' }
    $button.Height = 34
    $button.FontSize = 15
    $button.Margin = [Windows.Thickness]::new(0,0,0,6)
    $quit = $Window.FindName('Quit')
    $at = if ($quit -and $MainPage.Children.Contains($quit)) { $MainPage.Children.IndexOf($quit) } else { $MainPage.Children.Count }
    $MainPage.Children.Insert($at, $button)

    Add-Type -AssemblyName System.Net.Http
    $client = [Net.Http.HttpClient]::new()
    $client.Timeout = [TimeSpan]::FromSeconds(12)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('HOTD-Local-Builder-Updater/1')
    $state = @{ Task=$null; Release=$null; Requested=$false }
    $uri = "https://api.github.com/repos/davidptrssvea/$($game.Repo)/releases/latest"
    $timer = [Windows.Threading.DispatcherTimer]::new()
    $timer.Interval = [TimeSpan]::FromMilliseconds(250)
    $parseVersion = ${function:ConvertTo-HotdVersion}
    $validatePackage = ${function:Get-HotdReleasePackage}
    $powerShell = Get-HotdPowerShell
    $showRelease = {
        if (!$state.Release) { return }
        $release = $state.Release
        $latest = & $parseVersion $release.tag_name
        $dialog = [Windows.Window]::new()
        $dialog.Owner=$Window; $dialog.Title='HOTD updates'; $dialog.Width=620; $dialog.Height=510
        $dialog.WindowStartupLocation='CenterOwner'; $dialog.ShowInTaskbar=$false
        $panel=[Windows.Controls.DockPanel]::new(); $panel.Margin=[Windows.Thickness]::new(18)
        $heading=[Windows.Controls.TextBlock]::new()
        $heading.Text = "Installed: $(if ($installed) {$tag} else {'unknown'})     Latest: $($release.tag_name)"
        if ($installed -and $latest -gt $installed) {
            $heading.Text += "`nThe installer builds locally, keeps your settings and reopens the menu."
        }
        $heading.Margin=[Windows.Thickness]::new(0,0,0,12)
        [Windows.Controls.DockPanel]::SetDock($heading,'Top'); [void]$panel.Children.Add($heading)
        $actions=[Windows.Controls.StackPanel]::new()
        $actions.Orientation='Horizontal'; $actions.HorizontalAlignment='Right'
        $actions.Margin=[Windows.Thickness]::new(0,12,0,0)
        [Windows.Controls.DockPanel]::SetDock($actions,'Bottom'); [void]$panel.Children.Add($actions)
        $close=[Windows.Controls.Button]::new(); $close.Content='Close'; $close.MinWidth=80; $close.IsCancel=$true
        if ($installed -and $latest -gt $installed) {
            $apply=[Windows.Controls.Button]::new(); $apply.Content='Download and install'; $apply.MinWidth=150
            $apply.Margin=[Windows.Thickness]::new(0,0,8,0)
            $apply.Add_Click({ $dialog.DialogResult=$true }.GetNewClosure())
            [void]$actions.Children.Add($apply)
        }
        [void]$actions.Children.Add($close)
        $notes=[Windows.Controls.TextBox]::new(); $notes.IsReadOnly=$true; $notes.TextWrapping='Wrap'
        $notes.VerticalScrollBarVisibility='Auto'; $notes.Text=[string]$release.body
        [void]$panel.Children.Add($notes); $dialog.Content=$panel
        if ($dialog.ShowDialog() -ne $true) { return }
        try {
            $package = & $validatePackage $game $release
            $root = Split-Path -Parent $App
            $work = Join-Path $root ('.hotd-update-'+[guid]::NewGuid().ToString('N').Substring(0,10))
            [void][IO.Directory]::CreateDirectory($work)
            $script = Join-Path $work 'hotd-update.ps1'
            [IO.File]::Copy((Join-Path $App 'hotd-update.ps1'), $script)
            $request = Join-Path $work 'request.json'
            [IO.File]::WriteAllText($request, (@{ App=$App; Game=$Edition; Owner=$PID; Package=$package } | ConvertTo-Json -Compress))
            $info=[Diagnostics.ProcessStartInfo]::new()
            $info.FileName=$powerShell; $info.Arguments='-NoProfile -ExecutionPolicy Bypass -STA -File "{0}" -UpdateRequest "{1}"' -f $script,$request
            $info.WorkingDirectory=$work; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
            $process=[Diagnostics.Process]::Start($info)
            $progress=[Windows.Window]::new()
            $progress.Owner=$Window; $progress.Title='Installing HOTD update'; $progress.Width=490; $progress.Height=170
            $progress.WindowStartupLocation='CenterOwner'; $progress.ShowInTaskbar=$false; $progress.ResizeMode='NoResize'
            $stack=[Windows.Controls.StackPanel]::new(); $stack.Margin=[Windows.Thickness]::new(20)
            $label=[Windows.Controls.TextBlock]::new(); $label.Text='Downloading and verifying the installer...'; $label.TextWrapping='Wrap'
            $bar=[Windows.Controls.ProgressBar]::new(); $bar.Height=12; $bar.IsIndeterminate=$true; $bar.Margin=[Windows.Thickness]::new(0,14,0,0)
            [void]$stack.Children.Add($label); [void]$stack.Children.Add($bar); $progress.Content=$stack
            $signal=@{ Close=$false; Install=$false; Error=$null }
            $progress.Add_Closing({ param($sender,$e) $e.Cancel=!$signal.Close }.GetNewClosure())
            $poll=[Windows.Threading.DispatcherTimer]::new(); $poll.Interval=[TimeSpan]::FromMilliseconds(400)
            $status=Join-Path $work 'status.txt'
            $poll.Add_Tick({
                $text = if ([IO.File]::Exists($status)) { try { [IO.File]::ReadAllText($status) } catch {} }
                if ($text -eq 'Ready') { $signal.Install=$true }
                elseif ($text -like 'Error:*') { $signal.Error=$text }
                elseif ($process.HasExited) { $signal.Error="The updater closed early. Update files and logs: $work" }
                elseif ($text) { $label.Text=$text }
                if ($signal.Install -or $signal.Error) { $poll.Stop(); $signal.Close=$true; $progress.Close() }
            }.GetNewClosure())
            $poll.Start()
            try { [void]$progress.ShowDialog() } finally { $poll.Stop(); $process.Dispose() }
            if ($signal.Install) { $Window.Close() }
            elseif ($signal.Error) { [void][Windows.MessageBox]::Show($Window,$signal.Error,'HOTD update','OK','Error') }
        } catch { [void][Windows.MessageBox]::Show($Window,$_.Exception.Message,'HOTD update','OK','Error') }
    }.GetNewClosure()
    $check = {
        if ($state.Task) { return }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $state.Task=$client.GetStringAsync($uri)
        $button.Content='Checking for updates...'
        $timer.Start()
    }.GetNewClosure()
    $timer.Add_Tick({
        if (!$state.Task.IsCompleted) { return }
        $timer.Stop()
        try {
            $release=$state.Task.GetAwaiter().GetResult() | ConvertFrom-Json
            [void](& $validatePackage $game $release)
            $state.Release=$release
            $button.Content=if ($installed -and (& $parseVersion $release.tag_name) -gt $installed) {
                "Update available ($($release.tag_name))"
            } elseif ($installed) { "Updates ($tag)" } else { "Updates ($($release.tag_name))" }
        } catch { $button.Content='Updates unavailable. Retry' }
        $state.Task=$null
        if ($state.Requested -and $state.Release) { $state.Requested=$false; & $showRelease }
    }.GetNewClosure())
    $button.Add_Click({ if ($state.Release) { & $showRelease } else { $state.Requested=$true; & $check } }.GetNewClosure())
    $Window.Add_ContentRendered({ & $check }.GetNewClosure())
    $Window.Add_Closed({ $timer.Stop(); $client.Dispose() }.GetNewClosure())
}

if ($UpdateRequest) { Invoke-HotdUpdateWorker $UpdateRequest }
