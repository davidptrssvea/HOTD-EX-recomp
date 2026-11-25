param([switch]$RegisterOnly,[string]$LibraryRecompDir,
      [ValidateSet('Normal','Special','EX','Scarlet')][string]$LibraryGame='Normal')

# WPF event handlers run from dynamic modules, including in the embedded launcher.
function global:Get-HotdLibraryGames {
    'Normal','Special','EX','Scarlet'
}

function global:Get-HotdLibrarySpec([string]$Game) {
    switch ($Game) {
        Normal  { @{Title='The House of the Dead 4'; App='HOTD4_Recomp'; Packages=@('HOTD4-PC','HOTD4','HOTD 4'); Launcher='Play HOTD4.exe'; Menu='hotd4-startmenu.ps1'; Binary='hotd4-desktop.exe'; Accent='#E89869'} }
        Special { @{Title='The House of the Dead 4 Special'; App='HOTD4SP_Recomp'; Packages=@('HOTD4SP-PC','HOTD4 Special','HOTD 4 Special'); EntryScript='hotd4sp-launcher.ps1'; Launcher='HOTD4SP_Recomp/Play HOTD4 Special.exe'; Menu='hotd4sp-startmenu.ps1'; Binary='hotd4sp-desktop.exe'; Accent='#F37580'} }
        EX      { @{Title='The House of the Dead EX'; App='HOTDEX_Recomp'; Packages=@('HOTDEX-PC','HOTD EX'); EntryScript='config_menu.ps1'; Launcher='Play HOTD EX.exe'; Menu='hotdex-startmenu.ps1'; Binary='hotdex-desktop.exe'; Accent='#76D3DB'} }
        Scarlet { @{Title='House of the Dead: Scarlet Dawn'; App=''; Packages=@('ScarletDawn-PC','HOTDSD-PC','Scarlet Dawn'); EntryScript='scarlet-menu.xaml'; Launcher='Play Scarlet Dawn.exe'; Menu='scarlet-menu.ps1'; Binary='scarlet-settings.ps1'; Accent='#DDA1EA'} }
    }
}

function global:Get-HotdLibraryPath {
    Join-Path $env:LOCALAPPDATA 'HOTD4-Launcher/game-library.json'
}

function global:Read-HotdLibraryFile([string]$Path) {
    try {
        if (![IO.File]::Exists($Path)) { return }
        $share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
        $stream=[IO.File]::Open($Path,'Open','Read',$share)
        try {
            $reader=[IO.StreamReader]::new($stream)
            try { return ($reader.ReadToEnd() | ConvertFrom-Json) }
            finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    } catch { Write-Verbose ('Could not read the game library file: '+$_.Exception.Message) }
}

function global:Read-Hotd4Library([string]$LibraryFile=(Get-HotdLibraryPath)) {
    $result=@{}
    $data=Read-HotdLibraryFile $LibraryFile
    if ($data -and $data.Version -eq 1) {
        foreach ($game in Get-HotdLibraryGames) {
            $property=$data.PSObject.Properties[$game]
            if ($property -and $property.Value -is [string]) { $result[$game]=$property.Value }
        }
    }
    return $result
}

function global:Test-HotdLibraryData([string]$Root,[string]$Game) {
    if ($Game -eq 'Scarlet') {
        $data=Join-Path $Root 'Game/Hodzero'
        return ([IO.File]::Exists((Join-Path $data 'Binaries/Win64/Hodzero-Win64-Shipping.exe')) -and
                [IO.File]::Exists((Join-Path $data 'Content/Paks/Hodzero-WindowsNoEditor.pak')))
    }

    $elf='hod4M.elf'
    switch ($Game) {
        Normal {
            $config=Join-Path $Root 'dump-path.txt'
            if ([IO.File]::Exists($config)) {
                $dump=[IO.File]::ReadAllText($config).Trim()
                if (![IO.Path]::IsPathRooted($dump)) { return $false }
            } elseif ([IO.File]::Exists((Join-Path $Root 'Spela HOTD4 i 4K.cmd'))) {
                $dump=Join-Path $Root 'The House of the Dead 4/disk0'
            } else { return $false }
        }
        Special { $dump=Join-Path $Root 'hotd4sp' }
        EX { $dump=Join-Path $Root 'disk0'; $elf='hodexRI.elf' }
    }
    return ([IO.File]::Exists((Join-Path $dump ('elf/'+$elf))) -and
            [IO.Directory]::Exists((Join-Path $dump 'fs')))
}

function global:Resolve-Hotd4Installation {
    param([string]$Path,[ValidateSet('Normal','Special','EX','Scarlet')][string]$Game)
    if (!$Path) { return }
    $spec=Get-HotdLibrarySpec $Game
    try {
        if (![IO.Path]::IsPathRooted($Path)) { return }
        $selected=[IO.Path]::GetFullPath($Path)
        if ([IO.File]::Exists($selected)) { $selected=[IO.Path]::GetDirectoryName($selected) }
        if ($spec.App -and (Split-Path $selected -Leaf) -eq $spec.App) {
            $selected=[IO.Path]::GetDirectoryName($selected.TrimEnd('\','/'))
        }
        $roots=@($selected; foreach ($package in $spec.Packages) { Join-Path $selected $package })
        foreach ($root in $roots) {
            $app=if ($spec.App) { Join-Path $root $spec.App } else { $root }
            $launcher=Join-Path $root $spec.Launcher
            $menu=Join-Path $app $spec.Menu
            if (![IO.File]::Exists($launcher) -or ![IO.File]::Exists($menu) -or
                ![IO.File]::Exists((Join-Path $app $spec.Binary)) -or !(Test-HotdLibraryData $root $Game)) { continue }
            if ($spec.EntryScript -and ![IO.File]::Exists((Join-Path $app $spec.EntryScript))) { continue }
            $normalCmd=Join-Path $root 'Play HOTD4.cmd'
            if ($Game -eq 'Normal' -and ![IO.File]::Exists($normalCmd) -and
                ![IO.File]::Exists((Join-Path $root 'Spela HOTD4 i 4K.cmd'))) { continue }
            $supportsLibrary=if ($Game -eq 'Scarlet') {
                (Get-Content -LiteralPath $menu -TotalCount 5) -contains '# HOTD Game Library'
            } else { [IO.File]::Exists((Join-Path $app 'hotd-game-library.ps1')) }
            $legacyDirect=$Game -eq 'Normal' -and [IO.File]::Exists($normalCmd) -and
                (Get-Content -LiteralPath $normalCmd -TotalCount 20 -ErrorAction Stop | Select-String -Quiet 'HOTD4_NO_LAUNCHER')
            return [pscustomobject]@{
                Game=$Game; Title=$spec.Title; App=$app; Root=$root; Launcher=$launcher; Accent=$spec.Accent
                CanPlay=($supportsLibrary -or $Game -eq 'EX' -or $legacyDirect)
                CanSettings=($supportsLibrary -or $Game -in 'Special','EX')
            }
        }
    } catch { Write-Verbose ('Could not check an installation: '+$_.Exception.Message) }
}

function global:Register-Hotd4Installation {
    param([string]$RecompDir,[ValidateSet('Normal','Special','EX','Scarlet')][string]$Game,
          [string]$LibraryFile=(Get-HotdLibraryPath))
    $installation=Resolve-Hotd4Installation $RecompDir $Game
    if (!$installation) { throw 'Select a complete HOTD installation with its launcher and game data.' }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LibraryFile))
    $lock=$null
    $pending=$LibraryFile+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try {
        for ($attempt=0;$attempt -lt 10;$attempt++) {
            try {
                $lock=[IO.File]::Open(($LibraryFile+'.lock'),'OpenOrCreate','ReadWrite','None')
                break
            } catch [IO.IOException] {
                if ($attempt -eq 9) { throw }
                Start-Sleep -Milliseconds 100
            }
        }
        $data=Read-Hotd4Library $LibraryFile
        if ($data[$Game] -ceq $installation.App) { return }
        $data.Version=1
        $data[$Game]=$installation.App
        [IO.File]::WriteAllText($pending,($data | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        if ([IO.File]::Exists($LibraryFile)) {
            [IO.File]::Replace($pending,$LibraryFile,[NullString]::Value)
        } else { [IO.File]::Move($pending,$LibraryFile) }
    } finally {
        if ($lock) { $lock.Dispose() }
        if ([IO.File]::Exists($pending)) { [IO.File]::Delete($pending) }
    }
}

function global:Get-HotdLibraryInstallations {
    param([string]$RecompDir,[ValidateSet('Normal','Special','EX','Scarlet')][string]$CurrentGame,
          [string]$LibraryFile=(Get-HotdLibraryPath))
    $current=Resolve-Hotd4Installation $RecompDir $CurrentGame
    $saved=Read-Hotd4Library $LibraryFile
    $legacy=Read-Hotd4Library (Join-Path $env:LOCALAPPDATA 'HOTD4-Launcher/installations.json')
    $nearby=@($RecompDir)
    if ($current) {
        $nearby+=@($current.Root,(Split-Path $current.Root -Parent))
    } elseif ($RecompDir) {
        $nearby+=Split-Path $RecompDir -Parent
    }

    foreach ($game in Get-HotdLibraryGames) {
        if ($game -eq $CurrentGame -and $current) { $current; continue }
        $history=Read-HotdLibraryFile (Join-Path $env:LOCALAPPDATA ('HOTD-Setup/'+$game+'.json'))
        $remembered=if ($history -and $history.Format -eq 1 -and $history.Edition -ceq $game) { $history.InstallDir }
        foreach ($candidate in @($saved[$game],$legacy[$game],$remembered)+$nearby) {
            $installation=Resolve-Hotd4Installation $candidate $game
            if ($installation) { $installation; break }
        }
    }
}

function global:New-Hotd4GameStartInfo {
    param([string]$RecompDir,[ValidateSet('Normal','Special','EX','Scarlet')][string]$Game,
          [string]$PreviousRecompDir,[ValidateSet('Menu','Play','Settings')][string]$Action='Menu')
    $installation=Resolve-Hotd4Installation $RecompDir $Game
    if (!$installation) { throw 'This installation was moved or is incomplete. Use Locate installed game to find it again.' }
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$installation.Launcher
    $info.WorkingDirectory=[IO.Path]::GetDirectoryName($info.FileName)
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    if ($Action -eq 'Settings' -and $installation.CanSettings) { $info.Arguments='--settings' }
    if ($Action -eq 'Play' -and $installation.CanPlay) { $info.Arguments='--no-launcher' }
    foreach ($key in @($info.EnvironmentVariables.Keys)) {
        if ($key -match '^(HOTD4(?:SP)?_|HOTDEX_|SCARLET_|LINDBERGH_|RECOMP_|TEA_)') {
            $info.EnvironmentVariables.Remove($key)
        }
    }
    if ($PreviousRecompDir -and $info.EnvironmentVariables['PATH']) {
        $previous=$PreviousRecompDir.TrimEnd('\','/')
        $parts=$info.EnvironmentVariables['PATH'] -split ';' | Where-Object { $_.Trim('"').TrimEnd('\','/') -ine $previous }
        $info.EnvironmentVariables['PATH']=$parts -join ';'
    }
    return $info
}

function global:Get-HotdLibraryProfile($Installation) {
    $app=$Installation.App
    $read = {
        param([string]$Name)
        $path=Join-Path $app $Name
        try {
            if ([IO.File]::Exists($path)) { [IO.File]::ReadAllText($path).Trim() }
        } catch { Write-Verbose $_.Exception.Message }
    }
    $profile=@{Version=''; Display='Game defaults'; Input='Game defaults'; Extra=''; Repository=''}
    if ($Installation.Game -eq 'Scarlet') {
        $profile.Repository='HOTD-Scarlet-Dawn-recomp'
        $profile.Version=& $read 'scarlet-version.txt'
        $saved=Read-HotdLibraryFile (Join-Path $app 'Local/Settings.json')
        if ($saved.Resolution -match '^\d+x\d+$' -and $saved.Display -in 'Windowed','Borderless') {
            $profile.Display=($saved.Resolution -replace 'x',' × ')+' · '+$saved.Display
        }
        if ($saved.InputMode -in 'Mouse','Sinden','RawInput','Gamepad') {
            $profile.Input=switch ($saved.InputMode) { RawInput { 'Lightgun (Raw Input)' } Gamepad { 'Gamepad (XInput)' } default { $saved.InputMode } }
        }
        if ($saved.InputMode -eq 'Sinden' -and $saved.OffscreenReload -eq $true) {
            $profile.Extra=if ($saved.SindenSwitchWeaponOnReload -eq $true) {'Reload or switch weapon'} else {'Off-screen reload'}
        }
    } elseif ($Installation.Game -eq 'EX') {
        $profile.Repository='HOTD-EX-recomp'
        $profile.Version=& $read 'hotd-update-version.txt'
        $config=Join-Path $app 'hotdex-config.ps1'
        if ([IO.File]::Exists($config)) {
            try { $saved=& { . $config; Read-HotdexSettings (Join-Path $app 'lindbergh.ini') } }
            catch { Write-Verbose $_.Exception.Message; $saved=$null }
            if ($saved) {
                $mode=if ($saved.fullscreen) {'Fullscreen'} else {'Windowed'}
                $profile.Display="$($saved.width) × $($saved.height) · $mode"
                $inputMode=& $read 'input-profile.txt'
                $exModes=@{'2'='Lightgun (Raw Input)';'3'='Co-op lightgun + mouse';'4'='Co-op lightgun + raw mouse';'5'='Gamepad (P1 + P2)'}
                $profile.Input=if ($inputMode -and $exModes.ContainsKey($inputMode)) {$exModes[$inputMode]} elseif ($saved.raw_mouse) {'Raw mouse'} else {'Mouse'}
                if ($saved.leave_reload) { $profile.Extra='Off-screen reload' }
            }
        }
    } else {
        $profile.Repository=if ($Installation.Game -eq 'Normal') {'HOTD4-recomp'} else {'HOTD-4-Special-recomp'}
        $profile.Version=& $read 'hotd-update-version.txt'
        $resolution=& $read 'display-profile.txt'
        $mode=& $read 'display-mode.txt'
        if ($resolution -match '^\d+x\d+$') {
            $profile.Display=$resolution -replace 'x',' × '
            if ($mode -in 'Windowed','Borderless','Fullscreen') { $profile.Display+=' · '+$mode }
        }
        $modes=@{'0'='Absolute mouse';'1'='Raw mouse';'2'='Lightgun (Raw Input)';'3'='Co-op lightgun + mouse';'4'='Co-op lightgun + raw mouse';'5'='Keyboard';'6'='Gamepad (P1 + P2)'}
        $inputMode=[string](& $read 'input-profile.txt')
        if ($inputMode -and $modes.ContainsKey($inputMode)) { $profile.Input=$modes[$inputMode] }
        if ((& $read 'sinden-profile.txt') -eq '1') { $profile.Extra='Sinden border enabled' }
    }
    $profile
}

function global:Show-HotdLibraryPointerTest($Owner) {
    $test=[Windows.Window]::new()
    $test.Owner=$Owner
    $test.Title='Pointer and buttons'
    $test.SizeToContent='WidthAndHeight'
    $test.WindowStartupLocation='CenterOwner'
    $test.ResizeMode='NoResize'
    $test.ShowInTaskbar=$false
    $test.Background='#171D28'
    $test.Foreground='#F3F5FA'
    $panel=[Windows.Controls.StackPanel]::new()
    $panel.Margin=[Windows.Thickness]::new(18)
    $label=[Windows.Controls.TextBlock]::new()
    $label.Text='Move the mouse or Sinden pointer, then press a button.'
    $label.Margin=[Windows.Thickness]::new(0,0,0,10)
    $area=[Windows.Controls.Canvas]::new()
    $area.Width=[Math]::Min(530,[Windows.SystemParameters]::WorkArea.Width-78)
    $area.Height=230
    $area.Background='#0D131D'
    $area.ClipToBounds=$true
    $marker=[Windows.Shapes.Ellipse]::new()
    $marker.Width=$marker.Height=14
    $marker.Stroke='#F3F5FA'
    $marker.StrokeThickness=2
    $marker.IsHitTestVisible=$false
    $marker.Visibility='Hidden'
    [void]$area.Children.Add($marker)
    $area.Add_MouseMove({
        param($sender,$eventArgs)
        $point=$eventArgs.GetPosition($area)
        [Windows.Controls.Canvas]::SetLeft($marker,$point.X-7)
        [Windows.Controls.Canvas]::SetTop($marker,$point.Y-7)
        $marker.Visibility='Visible'
        $label.Text='Pointer {0:F0}, {1:F0}' -f $point.X,$point.Y
    }.GetNewClosure())
    $area.Add_MouseLeave({ $marker.Visibility='Hidden' }.GetNewClosure())
    $area.Add_MouseDown({
        param($sender,$eventArgs)
        $label.Text=$eventArgs.ChangedButton.ToString()+' button pressed'
    }.GetNewClosure())
    $test.Add_PreviewKeyDown({
        param($sender,$eventArgs)
        if ($eventArgs.Key -ne 'Escape') { $label.Text=$eventArgs.Key.ToString()+' pressed' }
    }.GetNewClosure())
    $close=[Windows.Controls.Button]::new()
    $close.Content='Close'
    $close.IsCancel=$true
    $close.HorizontalAlignment='Right'
    $close.MinWidth=88
    $close.Margin=[Windows.Thickness]::new(0,12,0,0)
    foreach($element in $label,$area,$close) { [void]$panel.Children.Add($element) }
    $test.Content=$panel
    [void]$test.ShowDialog()
}

function global:Show-HotdGameLibrary {
    param($Owner,[string]$RecompDir,[string]$CurrentGame,[scriptblock]$Inspect)
    [xml]$markup=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="HOTD · Game Library" WindowStartupLocation="CenterOwner" ShowInTaskbar="False"
        ResizeMode="CanResizeWithGrip" Background="#10141C" Foreground="#F4F5FA"
        FontFamily="Segoe UI" UseLayoutRounding="True">
  <Window.Resources>
    <Style TargetType="TextBlock">
      <Setter Property="Foreground" Value="#F4F5FA"/>
    </Style>
    <Style TargetType="Button">
      <Setter Property="Foreground" Value="#F4F5FA"/>
      <Setter Property="Background" Value="#252D3C"/>
      <Setter Property="BorderBrush" Value="#43506A"/>
      <Setter Property="BorderThickness" Value="1"/>
      <Setter Property="Padding" Value="14,8"/>
      <Setter Property="MinHeight" Value="39"/>
      <Setter Property="FontSize" Value="14"/>
      <Setter Property="FontWeight" Value="SemiBold"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="Face" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}"
                    BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="7" Padding="{TemplateBinding Padding}">
              <ContentPresenter VerticalAlignment="Center" HorizontalAlignment="Center" RecognizesAccessKey="True"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Face" Property="BorderBrush" Value="#DC8AA7"/></Trigger>
              <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Face" Property="BorderBrush" Value="#FFF4F8"/></Trigger>
              <Trigger Property="IsPressed" Value="True"><Setter TargetName="Face" Property="Opacity" Value=".77"/></Trigger>
              <Trigger Property="IsEnabled" Value="False"><Setter TargetName="Face" Property="Opacity" Value=".52"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style TargetType="ListBoxItem">
      <Setter Property="Foreground" Value="#F4F5FA"/>
      <Setter Property="Padding" Value="9"/>
      <Setter Property="Margin" Value="0,0,0,5"/>
      <Setter Property="HorizontalContentAlignment" Value="Stretch"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="ListBoxItem">
            <Border x:Name="Row" CornerRadius="6" BorderThickness="1" BorderBrush="Transparent" Background="Transparent" Padding="{TemplateBinding Padding}">
              <ContentPresenter/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Row" Property="Background" Value="#232C3A"/></Trigger>
              <Trigger Property="IsSelected" Value="True"><Setter TargetName="Row" Property="Background" Value="#2D3648"/><Setter TargetName="Row" Property="BorderBrush" Value="#596682"/></Trigger>
              <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Row" Property="BorderBrush" Value="#F8E8EC"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>
  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>
    <Border Grid.Row="0" Background="#1B2230" BorderBrush="#394253" BorderThickness="0,0,0,1" Padding="22,15">
      <TextBlock Text="HOTD  ·  Game Library" FontSize="18" FontWeight="Bold"/>
    </Border>
    <Grid Grid.Row="1">
      <Grid.ColumnDefinitions><ColumnDefinition Width="235"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
      <Border Background="#1B2230" BorderBrush="#394253" BorderThickness="0,0,1,0" Padding="12,16">
        <DockPanel>
          <TextBlock Text="LIBRARY" DockPanel.Dock="Top" Foreground="#AEBAD3" FontSize="12" FontWeight="Bold" Margin="9,0,0,12"/>
          <ListBox x:Name="Games" Background="Transparent" BorderThickness="0" Padding="0"
                   ScrollViewer.HorizontalScrollBarVisibility="Disabled" AutomationProperties.Name="Games"/>
        </DockPanel>
      </Border>
      <Grid x:Name="Detail" Grid.Column="1" ClipToBounds="True" Background="#121721">
        <Image x:Name="GameImage" Stretch="UniformToFill" IsHitTestVisible="False">
          <Image.OpacityMask>
            <LinearGradientBrush StartPoint="0,0.5" EndPoint="1,0.5">
              <GradientStop Color="#00000000" Offset="0"/>
              <GradientStop Color="#16000000" Offset="0.18"/>
              <GradientStop Color="#A7000000" Offset="0.46"/>
              <GradientStop Color="#FF000000" Offset="0.72"/>
            </LinearGradientBrush>
          </Image.OpacityMask>
        </Image>
        <Grid Margin="25,25,25,20">
          <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
          <DockPanel LastChildFill="True" Margin="0,0,0,10">
            <Border DockPanel.Dock="Right" Background="#EB1A202C" Padding="9,4" CornerRadius="5" VerticalAlignment="Top" Margin="12,0,0,0">
              <TextBlock x:Name="VersionText" FontFamily="Consolas" Foreground="#DFE7FA" FontSize="13"/>
            </Border>
            <StackPanel>
              <TextBlock Text="THE HOUSE OF THE DEAD" Foreground="#C2CAE0" FontWeight="SemiBold" FontSize="13"/>
              <TextBlock x:Name="GameTitle" FontSize="35" FontWeight="Bold" TextWrapping="Wrap" Margin="0,2,0,0"/>
            </StackPanel>
          </DockPanel>
          <ScrollViewer Grid.Row="1" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
            <StackPanel x:Name="GameContent">
              <WrapPanel Margin="0,0,0,19">
                <Button x:Name="Play" Content="_Play" Background="#BE0142" BorderBrush="#D74776" MinWidth="86" Margin="0,0,8,5"/>
                <Button x:Name="Settings" Content="_Settings" Margin="0,0,8,5"/>
                <Button x:Name="Menu" Content="_Menu" Margin="0,0,8,5"/>
              </WrapPanel>
              <WrapPanel>
                <Border Background="#E31A202C" BorderBrush="#71475469" BorderThickness="1" CornerRadius="7" Padding="13"
                        Width="248" Margin="0,0,12,12">
                  <StackPanel>
                    <TextBlock Text="Saved profile" FontWeight="Bold" FontSize="16" Margin="0,0,0,10"/>
                    <TextBlock Text="Display" Foreground="#BCC7DD" FontSize="12"/>
                    <TextBlock x:Name="DisplayText" FontWeight="SemiBold" TextWrapping="Wrap" Margin="0,2,0,9"/>
                    <TextBlock Text="Controls" Foreground="#BCC7DD" FontSize="12"/>
                    <TextBlock x:Name="InputText" FontWeight="SemiBold" TextWrapping="Wrap" Margin="0,2,0,0"/>
                    <TextBlock x:Name="ExtraText" Foreground="#DFE6F6" TextWrapping="Wrap" FontSize="12" Margin="0,5,0,0"/>
                  </StackPanel>
                </Border>
                <Border Background="#E31A202C" BorderBrush="#71475469" BorderThickness="1" CornerRadius="7" Padding="13"
                        Width="203" Margin="0,0,0,12">
                  <StackPanel>
                    <TextBlock Text="Maintenance" FontWeight="Bold" FontSize="16" Margin="0,0,0,10"/>
                    <Button x:Name="Updates" Content="Check for updates" Margin="0,0,0,7"/>
                    <Button x:Name="InputTest" Content="Input test"/>
                  </StackPanel>
                </Border>
              </WrapPanel>
              <Border x:Name="Feedback" Visibility="Collapsed" Background="#E31A202C" Padding="10,6" CornerRadius="6" HorizontalAlignment="Left">
                <TextBlock x:Name="UpdateStatus" TextWrapping="Wrap" AutomationProperties.LiveSetting="Polite"/>
              </Border>
            </StackPanel>
          </ScrollViewer>
        </Grid>
      </Grid>
    </Grid>
    <Border Grid.Row="2" Background="#1B2230" BorderBrush="#394253" BorderThickness="0,1,0,0" Padding="18,10">
      <DockPanel>
        <Button x:Name="Back" Content="_Back" DockPanel.Dock="Right" IsCancel="True" MinWidth="78"/>
        <Button x:Name="Locate" Content="_Locate installed game..." DockPanel.Dock="Left"/>
        <TextBlock x:Name="Status" Foreground="#FFD0DB" TextWrapping="Wrap" VerticalAlignment="Center" Margin="12,0"/>
      </DockPanel>
    </Border>
  </Grid>
</Window>
'@
    $window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup))
    if ($Owner) { $window.Owner=$Owner }
    $workArea=[Windows.SystemParameters]::WorkArea
    $window.Width=[Math]::Min(1030,$workArea.Width-32)
    $window.Height=[Math]::Min(602,$workArea.Height-28)
    $window.MinWidth=[Math]::Min(674,$window.Width)
    $window.MinHeight=[Math]::Min(426,$window.Height)
    $ui=@{}
    foreach ($name in 'Games','GameImage','GameTitle','VersionText','GameContent','Play','Settings','Menu','DisplayText',
        'InputText','ExtraText','Updates','InputTest','UpdateStatus','Feedback','Status','Locate') {
        $ui[$name]=$window.FindName($name)
    }
    $artwork=Join-Path (Split-Path (Get-HotdLibraryPath) -Parent) 'game-art'
    $shortNames=@{Normal='HOTD 4';Special='HOTD 4 Special';EX='HOTD EX';Scarlet='Scarlet Dawn'}
    $state=@{Selection=$null; Profile=$null; Art=@{}; Client=$null; Task=$null; UpdateGame=$null; Results=@{}}
    $timer=[Windows.Threading.DispatcherTimer]::new()
    $timer.Interval=[TimeSpan]::FromMilliseconds(200)
    $showUpdateStatus={
        $item=$ui.Games.SelectedItem.Tag
        $message=if($item){$state.Results[$item.Game]}
        $ui.UpdateStatus.Text=[string]$message
        $ui.Feedback.Visibility=if($message){'Visible'}else{'Collapsed'}
        $ui.Updates.IsEnabled=($null -eq $state.Task)
    }.GetNewClosure()
    $selectGame={
        $item=$ui.Games.SelectedItem.Tag
        $ui.GameContent.Visibility=if($item){'Visible'}else{'Collapsed'}
        if(!$item){
            $ui.GameTitle.Text='No games found'
            $ui.VersionText.Text=''
            $ui.GameImage.Source=$null
            return
        }
        $profile=Get-HotdLibraryProfile $item
        $state.Profile=$profile
        $ui.GameTitle.Text=$shortNames[$item.Game]
        $ui.VersionText.Text=if($profile.Version){$profile.Version}else{'Installed'}
        $ui.DisplayText.Text=$profile.Display
        $ui.InputText.Text=$profile.Input
        $ui.ExtraText.Text=$profile.Extra
        $ui.ExtraText.Visibility=if($profile.Extra){'Visible'}else{'Collapsed'}
        $ui.Play.Visibility=if($item.CanPlay){'Visible'}else{'Collapsed'}
        $ui.Settings.Visibility=if($item.CanSettings){'Visible'}else{'Collapsed'}
        $ui.Menu.Visibility=if($item.Game -ne $CurrentGame -or !($item.CanPlay -or $item.CanSettings)){'Visible'}else{'Collapsed'}
        foreach($action in 'Play','Settings','Menu'){
            [Windows.Automation.AutomationProperties]::SetName($ui[$action],"$action - $($item.Title)")
        }
        if(!$state.Art.ContainsKey($item.Game)){
            $art=Join-Path $artwork ($item.Game+'.jpg')
            $bitmap=$null
            if([IO.File]::Exists($art)){
                try {
                    $file=[IO.File]::OpenRead($art)
                    try {
                        $bitmap=[Windows.Media.Imaging.BitmapImage]::new()
                        $bitmap.BeginInit()
                        $bitmap.CacheOption='OnLoad'
                        $bitmap.StreamSource=$file
                        $bitmap.EndInit()
                        $bitmap.Freeze()
                    } finally { $file.Dispose() }
                } catch { $bitmap=$null }
            }
            $state.Art[$item.Game]=$bitmap
        }
        $ui.GameImage.Source=$state.Art[$item.Game]
        & $showUpdateStatus
    }.GetNewClosure()
    $ui.Games.Add_SelectionChanged($selectGame)
    $refresh={
        $selected=$ui.Games.SelectedItem.Tag.Game
        $ui.Games.Items.Clear()
        $ui.Status.Text=''
        foreach($item in Get-HotdLibraryInstallations $RecompDir $CurrentGame){
            $row=[Windows.Controls.ListBoxItem]::new()
            $row.Tag=$item
            [Windows.Automation.AutomationProperties]::SetName($row,$item.Title)
            $label=[Windows.Controls.TextBlock]::new()
            $label.Text=$shortNames[$item.Game]
            $label.FontWeight='SemiBold'
            $label.TextWrapping='Wrap'
            $row.Content=$label
            [void]$ui.Games.Items.Add($row)
        }
        $preferred=$ui.Games.Items | Where-Object { $_.Tag.Game -eq $selected } | Select-Object -First 1
        if(!$preferred){$preferred=$ui.Games.Items | Where-Object { $_.Tag.Game -eq $CurrentGame } | Select-Object -First 1}
        if($preferred){$ui.Games.SelectedItem=$preferred}
        elseif($ui.Games.Items.Count){$ui.Games.SelectedIndex=0}
        else{& $selectGame;$ui.Status.Text='Locate an installed game to get started.'}
    }.GetNewClosure()
    foreach($action in 'Play','Settings','Menu'){
        $ui[$action].Add_Click({
            param($sender)
            $item=$ui.Games.SelectedItem.Tag
            if(!$item){return}
            $state.Selection=@{Installation=$item;Action=$sender.Name}
            $window.Close()
        }.GetNewClosure())
    }
    $ui.Updates.Add_Click({
        $item=$ui.Games.SelectedItem.Tag
        if(!$item -or $state.Task){return}
        try {
            if(!$state.Client){
                Add-Type -AssemblyName System.Net.Http
                [Net.ServicePointManager]::SecurityProtocol=[Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
                $state.Client=[Net.Http.HttpClient]::new()
                $state.Client.Timeout=[TimeSpan]::FromSeconds(10)
                $state.Client.DefaultRequestHeaders.UserAgent.ParseAdd('HOTD-Game-Library/1')
            }
            $state.UpdateGame=$item.Game
            $state.Results[$item.Game]='Checking for updates...'
            $state.Task=$state.Client.GetStringAsync("https://api.github.com/repos/davidptrssvea/$($state.Profile.Repository)/releases/latest")
            $timer.Start()
        } catch {
            $state.Results[$item.Game]='Update check unavailable.'
        }
        & $showUpdateStatus
    }.GetNewClosure())
    $timer.Add_Tick({
        if(!$state.Task.IsCompleted){return}
        $timer.Stop()
        $game=$state.UpdateGame
        try {
            $release=$state.Task.GetAwaiter().GetResult() | ConvertFrom-Json
            if($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^v\d{1,5}\.\d{1,5}\.\d{1,5}$'){
                throw 'Stable release information is unavailable.'
            }
            $entry=$ui.Games.Items | Where-Object {$_.Tag.Game -eq $game} | Select-Object -First 1
            $installed=if($entry){(Get-HotdLibraryProfile $entry.Tag).Version}
            $latest=$release.tag_name
            $state.Results[$game]=if($installed -notmatch '^v\d{1,5}\.\d{1,5}\.\d{1,5}$'){
                "Latest release $latest. See Updates in the game menu."
            }elseif([version]$latest.Substring(1) -gt [version]$installed.Substring(1)){
                "Update available $latest. Open the game menu and choose Updates to install it."
            }else{'This installation is up to date.'}
        }catch{$state.Results[$game]='Update check unavailable. Try again.'}
        $state.Task=$null
        & $showUpdateStatus
    }.GetNewClosure())
    $ui.InputTest.Add_Click({ Show-HotdLibraryPointerTest $window }.GetNewClosure())
    $ui.Locate.Add_Click({
        Add-Type -AssemblyName System.Windows.Forms
        $dialog=[Windows.Forms.FolderBrowserDialog]::new()
        $dialog.Description='Select the installation folder for HOTD4, HOTD4 Special, HOTD EX or Scarlet Dawn.'
        $dialog.ShowNewFolderButton=$false
        try {
            if($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){return}
            $found=@(foreach($game in Get-HotdLibraryGames){Resolve-Hotd4Installation $dialog.SelectedPath $game})
            if(!$found){throw 'No complete HOTD installation was found in that folder.'}
            foreach($item in $found){Register-Hotd4Installation $item.App $item.Game}
            & $refresh
        }catch{$ui.Status.Text=$_.Exception.Message}
        finally{$dialog.Dispose()}
    }.GetNewClosure())
    $window.Add_ContentRendered({
        $focus=if($ui.Games.Items.Count){$ui.Games}else{$ui.Locate}
        [void]$focus.Focus()
    }.GetNewClosure())
    $window.Add_Closed({
        $timer.Stop()
        if($state.Client){$state.Client.Dispose()}
    }.GetNewClosure())
    & $refresh
    if($Inspect){& $Inspect $window $ui.Games; return $state.Selection}
    [void]$window.ShowDialog()
    return $state.Selection
}

function global:Add-Hotd4GameSwitcher {
    param($Window,$MainPage,[string]$RecompDir,[ValidateSet('Normal','Special','EX','Scarlet')][string]$CurrentGame,
          $PlayButton,$SettingsButton)
    try { Register-Hotd4Installation $RecompDir $CurrentGame } catch { Write-Verbose $_.Exception.Message }
    $button=[Windows.Controls.Button]::new()
    $button.Name='GameLibrary'
    $button.Content='Game Library...'
    $button.FontSize=16
    $button.MinHeight=34
    # Put it beside the existing top-level actions without changing the Play default.
    $MainPage.Children.Insert([Math]::Min(1,$MainPage.Children.Count),$button)
    $button.Add_Click({
        try {
            $selected=Show-HotdGameLibrary $Window $RecompDir $CurrentGame
            if (!$selected) { return }
            if ($selected.Installation.Game -eq $CurrentGame) {
                $target=switch ($selected.Action) { Play { $PlayButton }; Settings { $SettingsButton } }
                if ($target) {
                    $target.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
                }
                return
            }
            $info=New-Hotd4GameStartInfo $selected.Installation.App $selected.Installation.Game $RecompDir $selected.Action
            $child=[Diagnostics.Process]::Start($info)
            if (!$child) { throw 'Could not open the selected game.' }
            $child.Dispose()
            $Window.Close()
        } catch {
            [void][Windows.MessageBox]::Show($Window,$_.Exception.Message,'Game Library','OK','Error')
        }
    }.GetNewClosure())
    return $button
}

if ($RegisterOnly) { Register-Hotd4Installation $LibraryRecompDir $LibraryGame }
