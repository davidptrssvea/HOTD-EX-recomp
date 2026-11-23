function Get-BuilderPathHistoryFile([ValidateSet('Normal','Special','EX')][string]$Edition) {
    Join-Path (Join-Path $env:LOCALAPPDATA 'HOTD-Setup') ($Edition+'.json')
}

function Read-BuilderPathHistory([string]$Edition) {
    try {
        $file=Get-BuilderPathHistoryFile $Edition
        if(![IO.File]::Exists($file)){return @{}}
        $data=[IO.File]::ReadAllText($file) | ConvertFrom-Json
        if($data.Format -ne 1 -or $data.Edition -cne $Edition){return @{}}
        $result=@{}
        foreach($key in 'DumpPath','InstallDir') {
            if($data.$key -is [string] -and [IO.Path]::IsPathRooted($data.$key)) {
                $result[$key]=[IO.Path]::GetFullPath($data.$key)
            }
        }
        return $result
    } catch { return @{} }
}

function Save-BuilderPathHistory([string]$Edition,[string]$DumpPath,[string]$InstallDir) {
    $temporary=$null;$backup=$null
    try {
        $file=Get-BuilderPathHistoryFile $Edition
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))
        $temporary=$file+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
        $data=@{Format=1;Edition=$Edition;DumpPath=[IO.Path]::GetFullPath($DumpPath);InstallDir=[IO.Path]::GetFullPath($InstallDir)}
        [IO.File]::WriteAllText($temporary,($data | ConvertTo-Json))
        if([IO.File]::Exists($file)){$backup=$temporary+'.previous';[IO.File]::Replace($temporary,$file,$backup)}
        else{[IO.File]::Move($temporary,$file)}
    } catch { Write-Warning "Installation succeeded, but Setup could not remember the selected folders: $($_.Exception.Message)" }
    finally {foreach($owned in @($temporary,$backup)){if($owned -and [IO.File]::Exists($owned)){try{[IO.File]::Delete($owned)}catch{Write-Verbose 'Setup history temporary file could not be removed.'}}}}
}

function Select-BuilderSetupFolder([string]$Description,[string]$SelectedPath) {
    $picker=[Windows.Forms.FolderBrowserDialog]::new()
    try {
        $picker.Description=$Description;$picker.ShowNewFolderButton=$true
        if($SelectedPath -and [IO.Directory]::Exists($SelectedPath)){$picker.SelectedPath=$SelectedPath}
        if($picker.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){return $picker.SelectedPath}
    } finally {$picker.Dispose()}
}

function New-BuilderPathDialog([string]$Edition,[string]$DumpPath,[string]$InstallDir) {
    Add-Type -AssemblyName System.Windows.Forms,System.Drawing
    $form=[Windows.Forms.Form]::new()
    $title=switch($Edition){Normal{'HOTD4'} Special{'HOTD4 Special'} EX{'HOTD EX'}}
    $form.Text="$title Setup - folders";$form.StartPosition='CenterScreen'
    $form.ClientSize=[Drawing.Size]::new(690,266);$form.MinimumSize=[Drawing.Size]::new(620,305)
    $form.AutoScaleMode='Dpi';$form.Font=[Drawing.Font]::new('Segoe UI',10)
    $intro=[Windows.Forms.Label]::new();$intro.Text='Confirm your folders, or use Browse to select different locations.'
    $intro.SetBounds(18,16,650,28);$intro.Anchor='Top,Left,Right';$form.Controls.Add($intro)
    $controls=@{Form=$form}
    $row=0
    foreach($key in 'DumpPath','InstallDir') {
        $y=54+$row*72
        $label=[Windows.Forms.Label]::new();$label.Text=if($key -eq 'DumpPath'){'Your extracted arcade dump'}else{'Recomp installation folder (existing or new)'}
        $label.SetBounds(18,$y,640,23);$label.Anchor='Top,Left,Right';$form.Controls.Add($label)
        $text=[Windows.Forms.TextBox]::new();$text.Text=if($key -eq 'DumpPath'){$DumpPath}else{$InstallDir}
        $text.SetBounds(18,($y+25),544,27);$text.Anchor='Top,Left,Right';$form.Controls.Add($text);$controls[$key]=$text
        $button=[Windows.Forms.Button]::new();$button.Text='Browse...';$button.SetBounds(572,($y+23),100,31);$button.Anchor='Top,Right'
        $description=$label.Text
        $button.Add_Click({$folder=Select-BuilderSetupFolder $description $text.Text;if($folder){$text.Text=$folder}}.GetNewClosure())
        $form.Controls.Add($button);$controls[$key+'Browse']=$button;$row++
    }
    $status=[Windows.Forms.Label]::new();$status.SetBounds(18,201,450,48);$status.Anchor='Bottom,Left,Right'
    $status.Text='Folders are remembered separately for each game after a successful install.'
    $form.Controls.Add($status);$controls.Status=$status
    $ok=[Windows.Forms.Button]::new();$ok.Text='Continue';$ok.SetBounds(478,221,94,31);$ok.Anchor='Bottom,Right'
    $ok.Add_Click({
        try {
            $dump=$controls.DumpPath.Text.Trim();$install=$controls.InstallDir.Text.Trim()
            if(!$dump -or ![IO.Directory]::Exists($dump)){throw 'Select an existing extracted arcade dump folder.'}
            if(!$install -or ![IO.Path]::IsPathRooted($install)){throw 'Select or enter an absolute installation folder.'}
            [void][IO.Path]::GetFullPath($install)
            $form.DialogResult=[Windows.Forms.DialogResult]::OK;$form.Close()
        } catch {$status.Text=$_.Exception.Message;$status.ForeColor=[Drawing.Color]::DarkRed}
    }.GetNewClosure())
    $form.Controls.Add($ok);$form.AcceptButton=$ok;$controls.Continue=$ok
    $cancel=[Windows.Forms.Button]::new();$cancel.Text='Cancel';$cancel.DialogResult='Cancel';$cancel.SetBounds(580,221,92,31);$cancel.Anchor='Bottom,Right'
    $form.Controls.Add($cancel);$form.CancelButton=$cancel;$controls.Cancel=$cancel
    return $controls
}

function Select-BuilderSetupPaths([string]$Edition,[string]$DefaultInstall,[string]$InstallDir) {
    $last=Read-BuilderPathHistory $Edition
    $install=if($InstallDir){$InstallDir}elseif($last.InstallDir){$last.InstallDir}else{$DefaultInstall}
    $ui=New-BuilderPathDialog $Edition $last.DumpPath $install
    try {
        if($ui.Form.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){return $null}
        return @{DumpPath=$ui.DumpPath.Text.Trim();InstallDir=$ui.InstallDir.Text.Trim()}
    } finally {$ui.Form.Dispose()}
}
