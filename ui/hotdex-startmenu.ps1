function Get-HotdexDesktopSize {
    if(-not ('HotdexDisplay' -as [type])){
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HotdexDisplay {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool EnumDisplaySettings(string device,int mode,IntPtr data);
 public static int[] Size(){IntPtr p=Marshal.AllocHGlobal(220);try{Marshal.Copy(new byte[220],0,p,220);Marshal.WriteInt16(p,68,220);if(!EnumDisplaySettings(null,-1,p))throw new InvalidOperationException("Cannot read display mode");return new[]{Marshal.ReadInt32(p,172),Marshal.ReadInt32(p,176)};}finally{Marshal.FreeHGlobal(p);}}
}
'@
    }
    return [HotdexDisplay]::Size()
}
function ConvertTo-HotdexMenuResolution([string]$Text,[int[]]$Desktop) {
    if($Text -match '^Desktop(?:\s+\(\d{3,5}\s*x\s*\d{3,5}\))?$') {return $Desktop}
    if($Text -match '^\s*(\d{3,5})\s*[xX]\s*(\d{3,5})\s*(?:\([^)]*\))?\s*$'){
        $w=[int]$Matches[1];$h=[int]$Matches[2]
        if($w -ge 640 -and $h -ge 480 -and $w -le 8192 -and $h -le 8192){return $w,$h}
    }
}
function Get-HotdexControlSummary([string]$Path) {
 $b=Get-HotdexBindings (Split-Path $Path -Parent)
 return "P1  Coin: $(Get-HotdexKeyName $b.coin1)   Start: $(Get-HotdexKeyName $b.start1)    |    P2  Coin: $(Get-HotdexKeyName $b.coin2)   Start: $(Get-HotdexKeyName $b.start2)`nShoot: $(Get-HotdexKeyName $b.trigger)     Reload: $(Get-HotdexKeyName $b.reload)`nPedal P1: $(Get-HotdexKeyName $b.action)     Pedal P2: $(Get-HotdexKeyName $b.action2)`nPause: $(Get-HotdexKeyName $b.pause)     Test: $(Get-HotdexKeyName $b.test)     Service: $(Get-HotdexKeyName $b.service)"
}
function Show-HotdexStartMenu {
    param([Parameter(Mandatory)][string]$RecompDir,[ValidateSet('Main','Settings')][string]$InitialPage='Main',[scriptblock]$Inspect,[switch]$InGame,[long]$GameWindow=0)
    Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
    . (Join-Path $RecompDir 'hotdex-config.ps1')
    . (Join-Path $RecompDir 'hotdex-input.ps1')
    Initialize-HotdexPlatform
    $ini=Join-Path $RecompDir 'lindbergh.ini'
    $desktop=Get-HotdexDesktopSize
    [xml]$xaml=[IO.File]::ReadAllText((Join-Path $RecompDir 'hotdex-menu.xaml'))
    $window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($xaml))
    $window.Width=[Windows.SystemParameters]::PrimaryScreenWidth;$window.Height=[Windows.SystemParameters]::PrimaryScreenHeight
    $ui=@{}
    foreach($node in $xaml.SelectNodes('//*[@Name]')){$ui[$node.Name]=$window.FindName($node.Name)}
    $background=Join-Path $RecompDir 'hotdex-menu.jpg'
    if([IO.File]::Exists($background)){
        $brush=[Windows.Media.ImageBrush]::new([Windows.Media.Imaging.BitmapImage]::new([uri]$background))
        $brush.Stretch='UniformToFill';$brush.AlignmentX='Left';$ui.Backdrop.Background=$brush
    }
    $session=@{Page='MainPage';Result=0;Capture=$null;Bindings=@{}}
    $pages='MainPage','OptionsPage','GraphicsPage','MousePage','AudioPage','OtherPage','DevicesPage','BindingsPage'
    $context=@{
        OptionsPage=@('MAKE IT YOURS','Choose your render resolution and display mode, then fine-tune graphics, mouse input and audio. Save to keep your changes.')
        GraphicsPage=@('A CLEARER VIEW','The 3D scene renders at your selected resolution. Anti-aliasing smooths edges; anisotropic filtering keeps angled surfaces clearer. V-sync applies after restarting Arcade. Original artwork retains its original detail.')
        MousePage=@('AIM YOUR WAY','Raw Relative uses mouse movement without Windows pointer acceleration and locks the pointer. F1 releases it. The extra mouse crosshair is separate from the game''s own aiming indicators.')
        AudioPage=@('SET THE VOLUME','Enable or mute the game and choose your master volume. Your selection is used by both launch shortcuts.')
        DevicesPage=@('TWO PLAYERS, TWO DEVICES','For two lightguns, select Lightgun (Raw Input) and assign a different device to each player. Auto chooses distinct absolute guns. For a gun and a Windows/Parsec mouse, choose the mixed co-op mode; the mouse controls P2. Use Space / Right Ctrl for the two pedals by default.')
        BindingsPage=@('YOUR CABINET, YOUR CONTROLS','Click a binding, then press a key or mouse button. Esc cancels capture. Duplicate assignments are rejected. Mouse bindings are available for shoot, reload and pedal. Save your changes on the Settings page.')
        OtherPage=@('THE ARCADE COLLECTION','Open another installed game with its own launcher, settings and save data.')
    }
    $showPage={param([string]$Page)
        foreach($p in $pages){$ui[$p].Visibility=if($p -eq $Page){'Visible'}else{'Collapsed'}}
        $session.Page=$Page;$ui.ControlSummary.Visibility=if($Page -eq 'MainPage'){'Visible'}else{'Collapsed'}
        $ui.ContextCard.Visibility=if($context.ContainsKey($Page)){'Visible'}else{'Collapsed'}
        if($context.ContainsKey($Page)){$ui.ContextTitle.Text=$context[$Page][0];$ui.ContextText.Text=$context[$Page][1]}
    }
    $addOption={param($Combo,[string]$Value,[string]$Label)
        $item=[Windows.Controls.ComboBoxItem]::new();$item.Tag=$Value;$item.Content=$Label;[void]$Combo.Items.Add($item)
    }
    foreach($size in @('1280x720','1360x768','1920x1080','2560x1440','3840x2160')){
        $label=switch($size){'1280x720'{'1280 x 720 (720p)'}'1360x768'{'1360 x 768 (Arcade)'}'1920x1080'{'1920 x 1080 (1080p)'}'2560x1440'{'2560 x 1440 (1440p)'}'3840x2160'{'3840 x 2160 (4K)'}}
        & $addOption $ui.Resolution $size $label
    }
    & $addOption $ui.Resolution ($desktop -join 'x') ("Desktop ($($desktop[0]) x $($desktop[1]))")
    foreach($pair in @(@('0','Windowed'),@('1','Fullscreen'))){& $addOption $ui.DisplayMode $pair[0] $pair[1]}
    foreach($pair in @(@('0','Off'),@('1','On (sync to display)'))){& $addOption $ui.VSync $pair[0] $pair[1]}
    foreach($pair in @(@('0','Off'),@('2','MSAA 2x'),@('4','MSAA 4x'))){& $addOption $ui.AntiAliasing $pair[0] $pair[1]}
    foreach($n in 1,2,4,8,16){& $addOption $ui.Filtering ([string]$n) $(if($n -eq 1){'Off'}else{"AF ${n}x"})}
    foreach($pair in @(@('0','Desktop Cursor'),@('1','Raw Relative'))){& $addOption $ui.MouseMode $pair[0] $pair[1]}
    foreach($pair in @(@('1','Show'),@('0','Hide'))){& $addOption $ui.Crosshair $pair[0] $pair[1]}
    foreach($pair in @(@('0','Mouse (Absolute)'),@('1','Mouse (Raw Relative)'),@('2','Lightguns (Raw Input)'),@('3','Co-op: Gun P1 + Mouse P2'),@('4','Co-op: Gun P1 + Mouse P2 (Relative)'))){& $addOption $ui.InputMode $pair[0] $pair[1]}
    foreach($pair in @(@('25','Extra Thin (0.75%)'),@('50','Thin (1.5%, default)'),@('75','Medium (2.25%)'),@('100','Original (3%)'))){& $addOption $ui.BorderWidth $pair[0] $pair[1]}
    $selectOption={param($Combo,$Value)foreach($item in $Combo.Items){if([string]$item.Tag -eq [string]$Value){$Combo.SelectedItem=$item;return}}}
    $bindingButtons=@{}
    $refreshBindings={foreach($row in $script:HotdexControlRows){if($bindingButtons.ContainsKey($row[0])){$bindingButtons[$row[0]].Content=Get-HotdexKeyName $session.Bindings[$row[0]]}}}
    foreach($row in $script:HotdexControlRows){
      if($row[0] -eq 'firemode'){continue}
      $grid=[Windows.Controls.Grid]::new();[void]$grid.ColumnDefinitions.Add([Windows.Controls.ColumnDefinition]::new());$col=[Windows.Controls.ColumnDefinition]::new();$col.Width=[Windows.GridLength]::new(140);[void]$grid.ColumnDefinitions.Add($col)
      $label=[Windows.Controls.TextBlock]::new();$label.Text=$row[1];$label.FontSize=13;$label.VerticalAlignment='Center';$label.TextWrapping='Wrap';$label.Margin='0,0,10,0';[void]$grid.Children.Add($label)
      $button=[Windows.Controls.Button]::new();$button.Tag=$row[0];$button.FontSize=13;$button.Height=35;$button.Margin='0,2,8,2';[Windows.Controls.Grid]::SetColumn($button,1);[void]$grid.Children.Add($button);$bindingButtons[$row[0]]=$button
      $button.Add_Click({param($sender,$e)$session.Capture=[string]$sender.Tag;$ui.BindingStatus.Text='Press a key or mouse button. Esc cancels.';$sender.Content='Listening...'})
      [void]$ui.BindingList.Children.Add($grid)
    }
    $captureBinding={param([int]$Code)
      $id=$session.Capture;if(!$id){return}
      if($Code -eq 27){$session.Capture=$null;& $refreshBindings;$ui.BindingStatus.Text='Capture cancelled.';return}
      if(!$Code){$ui.BindingStatus.Text='That key is unsupported. Choose another key.';return}
      if($Code -lt 0 -and $id -notin 'trigger','reload','action','trigger2','reload2','action2'){$ui.BindingStatus.Text='Choose a keyboard key for this control.';return}
      foreach($key in $session.Bindings.Keys){if($key -ne $id -and $session.Bindings[$key] -eq $Code){$ui.BindingStatus.Text='That input is already assigned. Choose another key.';return}}
      $session.Bindings[$id]=$Code;$session.Capture=$null;& $refreshBindings;$ui.BindingStatus.Text='Binding changed. Save on the Settings page.'
    }
    $updateDevices={
      $mode=if($ui.InputMode.SelectedItem){[int]$ui.InputMode.SelectedItem.Tag}else{0}
      $ui.Gun1.IsEnabled=$mode -ge 2;$ui.Gun2.IsEnabled=$mode -eq 2
      $ui.MouseMode.IsEnabled=$mode -lt 2
      if($updateGrab){& $updateGrab}
      if($mode -lt 2){$session.SyncMode=$true;$ui.MouseMode.SelectedIndex=$mode;$session.SyncMode=$false}
    }
    $refreshDevices={param([string]$First,[string]$Second)
      $paths=@([HotdexPlatform]::Devices());foreach($p in 0,1){$combo=if($p){$ui.Gun2}else{$ui.Gun1};$selected=if($p){$Second}else{$First};$combo.Items.Clear();& $addOption $combo 'auto' 'Auto: distinct absolute gun';& $addOption $combo 'disabled' 'Disabled'
        $number=0;foreach($path in $paths){$number++;$id=if($path -match '(VID_[^#]+)'){$Matches[1]}else{($path -split '#')[1]};& $addOption $combo $path ("Device $number - $id")}
        if($selected -and $selected -notin 'auto','disabled' -and $selected -notin $paths){& $addOption $combo $selected ('Not connected - '+$selected)}
        & $selectOption $combo $(if($selected){$selected}elseif($p){'disabled'}else{'auto'})
      };& $updateDevices
    }
    $ui.InputMode.Add_SelectionChanged({& $updateDevices})
    $ui.RefreshDevices.Add_Click({& $refreshDevices ([string]$ui.Gun1.SelectedItem.Tag) ([string]$ui.Gun2.SelectedItem.Tag)})
    $ui.ResetBindings.Add_Click({foreach($row in $script:HotdexControlRows){$session.Bindings[$row[0]]=[int]$row[2]};$session.Capture=$null;& $refreshBindings;$ui.BindingStatus.Text='Defaults selected. Save on the Settings page.'})
    $window.Add_PreviewMouseDown({param($sender,$e)if($session.Capture){$code=switch($e.ChangedButton){'Left'{-1}'Middle'{-2}'Right'{-3}'XButton1'{-4}'XButton2'{-5}};if($code){& $captureBinding $code;$e.Handled=$true}}})
    $updateBorder={
      $on=[bool]$ui.Sinden.IsChecked;$ui.TrackingBorder.Visibility=if($on){'Visible'}else{'Collapsed'}
      if($on -and $ui.BorderWidth.SelectedItem){$scale=[int]$ui.BorderWidth.SelectedItem.Tag/100.0;$edge=[Math]::Max([Math]::Ceiling($window.Height*.03*$scale),[Math]::Ceiling(8*$scale));$black=[Math]::Max([Math]::Ceiling($window.Height*.005*$scale),[Math]::Ceiling(2*$scale));$ui.TrackingBorder.BorderThickness=[Windows.Thickness]::new($edge);$ui.TrackingOutline.BorderThickness=[Windows.Thickness]::new($black)}
    }
    $ui.Sinden.Add_Checked({& $updateBorder});$ui.Sinden.Add_Unchecked({& $updateBorder});$ui.BorderWidth.Add_SelectionChanged({& $updateBorder});$window.Add_SizeChanged({& $updateBorder})
    $updateGrab={
        if($ui.InputMode.SelectedItem -and [int]$ui.InputMode.SelectedItem.Tag -ge 2){$ui.MouseGrab.IsChecked=$false;$ui.MouseGrab.IsEnabled=$false;return}
        $raw=$ui.MouseMode.SelectedItem -and $ui.MouseMode.SelectedItem.Tag -eq '1'
        $ui.MouseGrab.IsEnabled=!$raw;if($raw){$ui.MouseGrab.IsChecked=$true}
    }
    $loadSettings={
        $s=Read-HotdexSettings $ini;$tag="$($s.width)x$($s.height)"
        $ui.Resolution.SelectedIndex=-1;& $selectOption $ui.Resolution $tag
        if($ui.Resolution.SelectedIndex -lt 0){$ui.Resolution.Text="$($s.width) x $($s.height)"}
        & $selectOption $ui.DisplayMode $s.fullscreen;& $selectOption $ui.AntiAliasing $s.aa;& $selectOption $ui.Filtering $s.af
        & $selectOption $ui.MouseMode $s.raw_mouse;& $selectOption $ui.Crosshair $s.crosshair
        $ui.Sensitivity.Value=[double]$s.mouse_sens;$ui.SensitivityValue.Text=('{0:0.00}' -f $ui.Sensitivity.Value)
        $ui.MouseGrab.IsChecked=[bool]$s.mouse_grab;$ui.LeaveReload.IsChecked=[bool]$s.leave_reload
        $ui.AudioEnabled.IsChecked=!$s.muted;$ui.Volume.Value=$s.volume;$ui.VolumeValue.Text=([string][int]$s.volume)+'%'
        $session.Bindings=Get-HotdexBindings $RecompDir;$session.Capture=$null;& $refreshBindings
        $extra=Get-HotdexInputPreferences $RecompDir $s;& $selectOption $ui.VSync $extra.VSync;& $selectOption $ui.InputMode $extra.Mode;& $selectOption $ui.BorderWidth $extra.Width;$ui.Sinden.IsChecked=[string]$extra.Border -eq '1';& $refreshDevices $extra.Gun1 $extra.Gun2
        & $updateGrab;$ui.SettingsStatus.Text='';$ui.ControlSummaryText.Text=Get-HotdexControlSummary $ini
    }
    $saveSettings={
        $size=ConvertTo-HotdexMenuResolution $ui.Resolution.Text $desktop
        if(!$size){$ui.SettingsStatus.Text='Enter a resolution such as 2560 x 1440 (640 x 480 to 8192 x 8192).';return $false}
        try{
            if([int]$ui.InputMode.SelectedItem.Tag -eq 2 -and $ui.Gun1.SelectedItem.Tag -eq $ui.Gun2.SelectedItem.Tag -and $ui.Gun1.SelectedItem.Tag -notin 'auto','disabled'){throw 'Assign a different device to each player.'}
            $v=@{width=[int]$size[0];height=[int]$size[1];fullscreen=[int]$ui.DisplayMode.SelectedItem.Tag;
                aa=[int]$ui.AntiAliasing.SelectedItem.Tag;af=[int]$ui.Filtering.SelectedItem.Tag;
                raw_mouse=[int]$ui.MouseMode.SelectedItem.Tag;mouse_sens=[Math]::Round([double]$ui.Sensitivity.Value,2);
                crosshair=[int]$ui.Crosshair.SelectedItem.Tag;mouse_grab=[int][bool]$ui.MouseGrab.IsChecked;
                leave_reload=[int][bool]$ui.LeaveReload.IsChecked;muted=[int](!$ui.AudioEnabled.IsChecked);volume=[int]$ui.Volume.Value}
            foreach($row in $script:HotdexControlRows | Select-Object -First 11){$code=$session.Bindings[$row[0]];$v[$row[0]]=if($code -lt 0){'mouse'+(-$code)}else{$code}}
            $v.raw_mouse=[int]([int]$ui.InputMode.SelectedItem.Tag -eq 1)
            Write-HotdexInputPreferences $RecompDir @{Mode=$ui.InputMode.SelectedItem.Tag;Gun1=$ui.Gun1.SelectedItem.Tag;Gun2=$ui.Gun2.SelectedItem.Tag;Border=[int][bool]$ui.Sinden.IsChecked;Width=$ui.BorderWidth.SelectedItem.Tag;AA=$v.aa;VSync=$ui.VSync.SelectedItem.Tag;Offscreen=$v.leave_reload;Speed=$v.mouse_sens} $session.Bindings
            Write-HotdexSettings $ini $v;$ui.SettingsStatus.Text='';return $true
        }catch{$ui.SettingsStatus.Text=$_.Exception.Message;return $false}
    }
    $startGame={
        if($InGame){$window.Close();return}
        try{
            [void](New-HotdexStartInfo $RecompDir)
            $info=[Diagnostics.ProcessStartInfo]::new()
            $info.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
            $info.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $RecompDir 'hotdex-launcher.ps1')+'" -ReturnToMenu'
            $info.UseShellExecute=$false;$info.CreateNoWindow=$true
            $child=[Diagnostics.Process]::Start($info);$child.Dispose();$window.Close()
        }catch{[void][Windows.MessageBox]::Show($window,$_.Exception.Message,'HOTD EX - Launch error')}
    }
    $ui.DevicesOpen.Add_Click({& $showPage 'DevicesPage'});$ui.BindingsOpen.Add_Click({& $showPage 'BindingsPage'})
    foreach($name in 'DevicesBack','BindingsBack'){$ui[$name].Add_Click({$session.Capture=$null;& $refreshBindings;& $showPage 'OptionsPage'})}
    $ui.RestartArcade.Add_Click({$session.Result=74;$window.Close()});$ui.ReturnMain.Add_Click({$session.Result=75;$window.Close()})
    $ui.TestMenu.Add_Click({if($InGame){$session.Result=76;$window.Close()}else{& $startOperator}})
    $startOperator={
        $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe';$info.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $RecompDir 'hotdex-launcher.ps1')+'" -Mode Test -ReturnToMenu';$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$child=[Diagnostics.Process]::Start($info);$child.Dispose();$window.Close()
    }
    $operatorDump=if($InGame -and $env:TEA_DIR){$env:TEA_DIR}else{Join-Path (Split-Path $RecompDir -Parent) 'disk0'}
    $ui.TestMenu.IsEnabled=[IO.File]::Exists((Join-Path $RecompDir 'hotdex-testmenu.exe')) -and [IO.File]::Exists((Join-Path $operatorDump 'elf/hodextestR.elf'))
    $ui.TestMenu.ToolTip=if($ui.TestMenu.IsEnabled){'Ends the current game and opens the arcade operator settings. Service selects; Test confirms.'}else{'Requires the supported hodextestR.elf from your dump. Rerun Setup with that file present.'}
    [Windows.Controls.ToolTipService]::SetShowOnDisabled($ui.TestMenu,$true)
    if($InGame){$window.AllowsTransparency=$true;$window.Background=[Windows.Media.Brushes]::Transparent;$ui.Backdrop.Background=$null;$ui.Play.Content='Resume';$ui.RestartArcade.Visibility='Visible';$ui.ReturnMain.Visibility='Visible';$ui.RestartNotice.Visibility='Visible';$ui.SavePlay.Visibility='Collapsed';$ui.OtherGames.Visibility='Collapsed';$ui.CollectionCaption.Text='PAUSED';$ui.ControlFooter.Text='The game clock and audio are paused. Save, then Resume or Restart Arcade.'
      if($GameWindow){$bounds=[HotdexPlatform]::Bounds([intptr]$GameWindow);if($bounds){$window.Left=$bounds[0];$window.Top=$bounds[1];$window.Width=$bounds[2];$window.Height=$bounds[3]};$owner=[Windows.Interop.WindowInteropHelper]::new($window);$owner.Owner=[intptr]$GameWindow}
    }
    $ui.Play.Add_Click({& $startGame});$ui.Quit.Add_Click({if($InGame){$session.Result=77};$window.Close()})
    $ui.Options.Add_Click({& $loadSettings;& $showPage 'OptionsPage';[void]$ui.Resolution.Focus()})
    $ui.GraphicsOpen.Add_Click({& $showPage 'GraphicsPage';[void]$ui.AntiAliasing.Focus()})
    $ui.MouseOpen.Add_Click({& $showPage 'MousePage';[void]$ui.MouseMode.Focus()})
    $ui.AudioOpen.Add_Click({& $showPage 'AudioPage';[void]$ui.AudioEnabled.Focus()})
    foreach($name in 'GraphicsBack','MouseBack','AudioBack'){$ui[$name].Add_Click({& $showPage 'OptionsPage';[void]$ui.GraphicsOpen.Focus()})}
    $ui.OptionsBack.Add_Click({& $loadSettings;& $showPage 'MainPage';[void]$ui.Options.Focus()})
    $ui.Save.Add_Click({if(& $saveSettings){& $loadSettings;& $showPage 'MainPage';[void]$ui.Play.Focus()}})
    $ui.SavePlay.Add_Click({if(& $saveSettings){& $startGame}})
    $ui.MouseMode.Add_SelectionChanged({& $updateGrab;if(!$session.SyncMode -and $ui.InputMode.SelectedItem -and [int]$ui.InputMode.SelectedItem.Tag -lt 2){$ui.InputMode.SelectedIndex=$ui.MouseMode.SelectedIndex}})
    $ui.Sensitivity.Add_ValueChanged({$ui.SensitivityValue.Text=('{0:0.00}' -f $ui.Sensitivity.Value)})
    $ui.Volume.Add_ValueChanged({$ui.VolumeValue.Text=([string][int]$ui.Volume.Value)+'%'})
    $library=@{}
    try{$registry=Join-Path $env:LOCALAPPDATA 'HOTD4-Launcher/installations.json';if([IO.File]::Exists($registry)){$registered=[IO.File]::ReadAllText($registry)|ConvertFrom-Json;$library.Normal=$registered.Normal;$library.Special=$registered.Special}}catch{}
    $near=Join-Path (Split-Path (Split-Path $RecompDir -Parent) -Parent) 'HOTD4'
    foreach($game in 'Normal','Special'){
        $folder=if($game -eq 'Normal'){'HOTD4_Recomp'}else{'HOTD4SP_Recomp'}
        foreach($candidate in @((Join-Path $near $folder),$library[$game])){
            if(!$candidate){continue}
            $entry=if($game -eq 'Normal'){Join-Path (Split-Path $candidate -Parent) 'Play HOTD4.exe'}else{Join-Path $candidate 'Play HOTD4 Special.exe'}
            if(![IO.File]::Exists($entry)){continue}
            $b=[Windows.Controls.Button]::new();$b.Content=if($game -eq 'Normal'){'The House of the Dead 4'}else{'The House of the Dead 4 Special'};$b.Tag=$entry;$b.FontSize=17
            $b.Add_Click({param($sender,$eventArgs)
                try{$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=[string]$sender.Tag;$info.WorkingDirectory=Split-Path $info.FileName -Parent;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
                    foreach($key in @($info.EnvironmentVariables.Keys)){if($key -match '^(HOTD4_|HOTD4SP_|LINDBERGH_|RECOMP_)' -or $key -eq 'TEA_DIR'){$info.EnvironmentVariables.Remove($key)}}
                    $child=[Diagnostics.Process]::Start($info);$child.Dispose();$window.Close()
                }catch{[void][Windows.MessageBox]::Show($window,$_.Exception.Message,'Could not open game')}
            })
            [void]$ui.OtherGameList.Children.Add($b);$ui.OtherGames.Visibility='Visible';break
        }
    }
    $ui.OtherGames.Add_Click({& $showPage 'OtherPage'});$ui.OtherBack.Add_Click({& $showPage 'MainPage';[void]$ui.OtherGames.Focus()})
    if($InGame){$ui.OtherGames.Visibility='Collapsed'}
    $window.Add_PreviewKeyDown({param($sender,$eventArgs)
        if($session.Capture){& $captureBinding (ConvertTo-HotdexKey $eventArgs);$eventArgs.Handled=$true;return}
        if($eventArgs.Key -eq 'Escape'){
            foreach($key in 'Resolution','DisplayMode','AntiAliasing','VSync','Filtering','MouseMode','Crosshair'){if($ui[$key].IsDropDownOpen){return}}
            switch($session.Page){'MainPage'{$window.Close()}'OptionsPage'{& $loadSettings;& $showPage 'MainPage'}'OtherPage'{& $showPage 'MainPage'}default{& $showPage 'OptionsPage'}}
            $eventArgs.Handled=$true
        }elseif($eventArgs.Key -in 'Up','Down' -and $session.Page -in 'MainPage','OtherPage'){
            $focused=[Windows.Input.Keyboard]::FocusedElement
            if($focused -is [Windows.UIElement]){$direction=if($eventArgs.Key -eq 'Up'){'Previous'}else{'Next'};[void]$focused.MoveFocus([Windows.Input.TraversalRequest]::new([Windows.Input.FocusNavigationDirection]$direction));$eventArgs.Handled=$true}
        }
    })
    & $loadSettings;& $showPage $(if($InitialPage -eq 'Settings'){'OptionsPage'}else{'MainPage'})
    if(!$InGame){
        . (Join-Path $RecompDir 'hotd-update.ps1')
        Add-HotdUpdater $window $ui.MainPage $RecompDir EX
    }
    if($Inspect){& $Inspect $window $ui $showPage $saveSettings $loadSettings;if($InGame){return [int]$session.Result};return}
    $window.Add_ContentRendered({if($session.Page -eq 'MainPage'){[void]$ui.Play.Focus()}else{[void]$ui.Resolution.Focus()}})
    [void]$window.ShowDialog()
    if($InGame){return [int]$session.Result}
}
