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
    $bindings=@{coin1='53';start1='49';coin2='54';start2='50';trigger='mouse1';reload='mouse3';action='32'}
    foreach($line in [IO.File]::ReadAllLines($Path)){
        if($line -match '^\s*(coin1|start1|coin2|start2|trigger|reload|action)\s*=\s*([^#]+)'){$bindings[$Matches[1]]=$Matches[2].Trim()}
    }
    foreach($k in @($bindings.Keys)){
        $v=$bindings[$k]
        $bindings[$k]=switch($v){'mouse1'{'Left mouse'}'mouse2'{'Middle mouse'}'mouse3'{'Right mouse'}'32'{'Space'}'13'{'Enter'}'27'{'Esc'}'none'{'Unassigned'} default{if($v -match '^\d+$' -and [int]$v -ge 33 -and [int]$v -le 126){([char][int]$v).ToString().ToUpperInvariant()}else{$v}}}
    }
    return "P1  Coin: $($bindings.coin1)     Start: $($bindings.start1)       |       P2  Coin: $($bindings.coin2)     Start: $($bindings.start2)`nShoot: $($bindings.trigger)       Reload: $($bindings.reload)`nPedal / Action: $($bindings.action)       Extra crosshair: F2"
}
function Show-HotdexStartMenu {
    param([Parameter(Mandatory)][string]$RecompDir,[ValidateSet('Main','Settings')][string]$InitialPage='Main',[scriptblock]$Inspect)
    Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
    . (Join-Path $RecompDir 'hotdex-config.ps1')
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
    $session=@{Page='MainPage'}
    $pages='MainPage','OptionsPage','GraphicsPage','MousePage','AudioPage','OtherPage'
    $context=@{
        OptionsPage=@('MAKE IT YOURS','Choose your render resolution and display mode, then fine-tune graphics, mouse input and audio. Save to keep your changes.')
        GraphicsPage=@('A CLEARER VIEW','The 3D scene renders at your selected resolution. Anti-aliasing smooths edges; anisotropic filtering keeps angled surfaces clearer. Original artwork retains its original detail.')
        MousePage=@('AIM YOUR WAY','Raw Relative uses mouse movement without Windows pointer acceleration and locks the pointer. F1 releases it. The extra mouse crosshair is separate from the game''s own aiming indicators.')
        AudioPage=@('SET THE VOLUME','Enable or mute the game and choose your master volume. Your selection is used by both launch shortcuts.')
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
    foreach($pair in @(@('0','Off'),@('2','MSAA 2x'),@('4','MSAA 4x'))){& $addOption $ui.AntiAliasing $pair[0] $pair[1]}
    foreach($n in 1,2,4,8,16){& $addOption $ui.Filtering ([string]$n) $(if($n -eq 1){'Off'}else{"AF ${n}x"})}
    foreach($pair in @(@('0','Desktop Cursor'),@('1','Raw Relative'))){& $addOption $ui.MouseMode $pair[0] $pair[1]}
    foreach($pair in @(@('1','Show'),@('0','Hide'))){& $addOption $ui.Crosshair $pair[0] $pair[1]}
    $selectOption={param($Combo,$Value)foreach($item in $Combo.Items){if([string]$item.Tag -eq [string]$Value){$Combo.SelectedItem=$item;return}}}
    $updateGrab={
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
        & $updateGrab;$ui.SettingsStatus.Text='';$ui.ControlSummaryText.Text=Get-HotdexControlSummary $ini
    }
    $saveSettings={
        $size=ConvertTo-HotdexMenuResolution $ui.Resolution.Text $desktop
        if(!$size){$ui.SettingsStatus.Text='Enter a resolution such as 2560 x 1440 (640 x 480 to 8192 x 8192).';return $false}
        try{
            $v=@{width=[int]$size[0];height=[int]$size[1];fullscreen=[int]$ui.DisplayMode.SelectedItem.Tag;
                aa=[int]$ui.AntiAliasing.SelectedItem.Tag;af=[int]$ui.Filtering.SelectedItem.Tag;
                raw_mouse=[int]$ui.MouseMode.SelectedItem.Tag;mouse_sens=[Math]::Round([double]$ui.Sensitivity.Value,2);
                crosshair=[int]$ui.Crosshair.SelectedItem.Tag;mouse_grab=[int][bool]$ui.MouseGrab.IsChecked;
                leave_reload=[int][bool]$ui.LeaveReload.IsChecked;muted=[int](!$ui.AudioEnabled.IsChecked);volume=[int]$ui.Volume.Value}
            Write-HotdexSettings $ini $v;$ui.SettingsStatus.Text='';return $true
        }catch{$ui.SettingsStatus.Text=$_.Exception.Message;return $false}
    }
    $startGame={
        try{
            [void](New-HotdexStartInfo $RecompDir)
            $info=[Diagnostics.ProcessStartInfo]::new()
            $info.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
            $info.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $RecompDir 'hotdex-launcher.ps1')+'"'
            $info.UseShellExecute=$false;$info.CreateNoWindow=$true
            $child=[Diagnostics.Process]::Start($info);$child.Dispose();$window.Close()
        }catch{[void][Windows.MessageBox]::Show($window,$_.Exception.Message,'HOTD EX - Launch error')}
    }
    $ui.Play.Add_Click({& $startGame});$ui.Quit.Add_Click({$window.Close()})
    $ui.Options.Add_Click({& $loadSettings;& $showPage 'OptionsPage';[void]$ui.Resolution.Focus()})
    $ui.GraphicsOpen.Add_Click({& $showPage 'GraphicsPage';[void]$ui.AntiAliasing.Focus()})
    $ui.MouseOpen.Add_Click({& $showPage 'MousePage';[void]$ui.MouseMode.Focus()})
    $ui.AudioOpen.Add_Click({& $showPage 'AudioPage';[void]$ui.AudioEnabled.Focus()})
    foreach($name in 'GraphicsBack','MouseBack','AudioBack'){$ui[$name].Add_Click({& $showPage 'OptionsPage';[void]$ui.GraphicsOpen.Focus()})}
    $ui.OptionsBack.Add_Click({& $loadSettings;& $showPage 'MainPage';[void]$ui.Options.Focus()})
    $ui.Save.Add_Click({if(& $saveSettings){& $loadSettings;& $showPage 'MainPage';[void]$ui.Play.Focus()}})
    $ui.SavePlay.Add_Click({if(& $saveSettings){& $startGame}})
    $ui.MouseMode.Add_SelectionChanged({& $updateGrab})
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
    $window.Add_PreviewKeyDown({param($sender,$eventArgs)
        if($eventArgs.Key -eq 'Escape'){
            foreach($key in 'Resolution','DisplayMode','AntiAliasing','Filtering','MouseMode','Crosshair'){if($ui[$key].IsDropDownOpen){return}}
            switch($session.Page){'MainPage'{$window.Close()}'OptionsPage'{& $loadSettings;& $showPage 'MainPage'}'OtherPage'{& $showPage 'MainPage'}default{& $showPage 'OptionsPage'}}
            $eventArgs.Handled=$true
        }elseif($eventArgs.Key -in 'Up','Down' -and $session.Page -in 'MainPage','OtherPage'){
            $focused=[Windows.Input.Keyboard]::FocusedElement
            if($focused -is [Windows.UIElement]){$direction=if($eventArgs.Key -eq 'Up'){'Previous'}else{'Next'};[void]$focused.MoveFocus([Windows.Input.TraversalRequest]::new([Windows.Input.FocusNavigationDirection]$direction));$eventArgs.Handled=$true}
        }
    })
    & $loadSettings;& $showPage $(if($InitialPage -eq 'Settings'){'OptionsPage'}else{'MainPage'})
    . (Join-Path $RecompDir 'hotd-update.ps1')
    Add-HotdUpdater $window $ui.MainPage $RecompDir EX
    if($Inspect){& $Inspect $window $ui $showPage $saveSettings $loadSettings;return}
    $window.Add_ContentRendered({if($session.Page -eq 'MainPage'){[void]$ui.Play.Focus()}else{[void]$ui.Resolution.Focus()}})
    [void]$window.ShowDialog()
}
