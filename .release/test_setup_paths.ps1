$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/setup_paths.ps1')
function Assert($Condition,$Message){if(!$Condition){throw $Message}}
$previous=$env:LOCALAPPDATA
$sandbox=Join-Path ([IO.Path]::GetTempPath()) ('HOTD Setup paths '+[char]0xe5+' '+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($sandbox)
$env:LOCALAPPDATA=$sandbox
try {
 $dump=Join-Path $sandbox 'My dump';$install=Join-Path $sandbox 'Existing install'
 [void][IO.Directory]::CreateDirectory($dump);[void][IO.Directory]::CreateDirectory($install)
 foreach($edition in 'Normal','Special','EX'){
  Assert ((Read-BuilderPathHistory $edition).Count -eq 0) 'Missing history did not use defaults'
  Save-BuilderPathHistory $edition $dump ($install+' '+$edition)
  $loaded=Read-BuilderPathHistory $edition
  Assert ($loaded.DumpPath -ceq $dump -and $loaded.InstallDir -ceq ($install+' '+$edition)) 'Folder history roundtrip failed'
 }
 Save-BuilderPathHistory Special $dump (Join-Path $sandbox 'Special updated')
 Assert ((Read-BuilderPathHistory Normal).InstallDir -ceq ($install+' Normal')) 'Edition histories leaked'
 Assert ((Read-BuilderPathHistory Special).InstallDir -like '*Special updated') 'Atomic overwrite failed'
 [IO.File]::WriteAllText((Get-BuilderPathHistoryFile EX),'{broken')
 Assert ((Read-BuilderPathHistory EX).Count -eq 0) 'Corrupt history was not ignored'
 [IO.File]::WriteAllText((Get-BuilderPathHistoryFile EX),(@{Format=1;Edition='Normal';DumpPath=$dump;InstallDir=$install}|ConvertTo-Json))
 Assert ((Read-BuilderPathHistory EX).Count -eq 0) 'Wrong-edition history was accepted'
 [IO.File]::WriteAllText((Get-BuilderPathHistoryFile EX),(@{Format=1;Edition='EX';DumpPath='relative';InstallDir='relative'}|ConvertTo-Json))
 Assert ((Read-BuilderPathHistory EX).Count -eq 0) 'Relative history paths were accepted'
 'PASS defaults, spaces/Unicode, atomic overwrite, separate editions, corrupt/wrong-edition/relative history rejection'

 # Exercise the actual WinForms controls and Browse event handlers. Only the
 # OS folder picker is substituted so this test can run unattended on CI.
 function Select-BuilderSetupFolder([string]$Description,[string]$SelectedPath){$dump}
 $ui=New-BuilderPathDialog EX '' $install
 try {
  $ui.Form.StartPosition='Manual';$ui.Form.Location=[Drawing.Point]::new(-20000,-20000);$ui.Form.ShowInTaskbar=$false
  $ui.Form.Show();[Windows.Forms.Application]::DoEvents()
  $ui.Continue.PerformClick()
  Assert ($ui.Form.DialogResult -ne 'OK' -and $ui.Status.Text -like '*existing extracted*') 'Missing dump was accepted'
  $ui.DumpPathBrowse.PerformClick();Assert ($ui.DumpPath.Text -ceq $dump) 'Dump Browse did not update the editor'
  $ui.InstallDirBrowse.PerformClick();Assert ($ui.InstallDir.Text -ceq $dump) 'Install Browse did not update the editor'
  $ui.InstallDir.Text=Join-Path $sandbox 'A new installation'
  foreach($control in $ui.Form.Controls){Assert ($control.Bottom -le $ui.Form.ClientSize.Height -and $control.Right -le $ui.Form.ClientSize.Width) 'A Setup control is clipped'}
  $ui.Continue.PerformClick();Assert ($ui.Form.DialogResult -eq 'OK') 'Valid edited folders were rejected'
 } finally {$ui.Form.Dispose()}
 'PASS real Setup dialog editors/Browse, invalid-input feedback, new installation path, control bounds and Continue'
 Assert ([IO.File]::ReadAllText((Get-BuilderPathHistoryFile EX)) -like '*relative*') 'Selecting paths unexpectedly saved history before installation'
 'PASS folder selection alone does not overwrite last successful history'
 Save-BuilderPathHistory EX $dump $install
 $script:dialogResult=[Windows.Forms.DialogResult]::OK
 function New-BuilderPathDialog([string]$Edition,[string]$DumpPath,[string]$InstallDir){
  $form=[pscustomobject]@{}
  $form | Add-Member ScriptMethod ShowDialog {return $script:dialogResult}
  $form | Add-Member ScriptMethod Dispose {}
  return @{Form=$form;DumpPath=@{Text=$DumpPath};InstallDir=@{Text=$InstallDir}}
 }
 $selected=Select-BuilderSetupPaths EX (Join-Path $sandbox 'Default install') ''
 Assert ($selected.DumpPath -ceq $dump -and $selected.InstallDir -ceq $install) 'Saved folders did not prefill manual Setup'
 $explicit=Join-Path $sandbox 'Explicit output'
 $selected=Select-BuilderSetupPaths EX (Join-Path $sandbox 'Default install') $explicit
 Assert ($selected.InstallDir -ceq $explicit) 'Explicit installation path lost precedence'
 $script:dialogResult=[Windows.Forms.DialogResult]::Cancel
 $selected=Select-BuilderSetupPaths EX $explicit ''
 Assert ($null -eq $selected) 'Cancel was not propagated'
 Assert ((Read-BuilderPathHistory EX).InstallDir -ceq $install) 'Cancelled/edited dialog overwrote successful history'
 'PASS remembered prefill, explicit installation override and cancellation without saving'
} finally {$env:LOCALAPPDATA=$previous}
