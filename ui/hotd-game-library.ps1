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

function global:Show-HotdGameLibrary {
    param($Owner,[string]$RecompDir,[string]$CurrentGame,[scriptblock]$Inspect)
    [xml]$markup=@"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Title="The House of the Dead" WindowStartupLocation="CenterOwner" ShowInTaskbar="False"
        ResizeMode="NoResize" SizeToContent="Height" Background="#111419" Foreground="#F2EEEB"
        FontFamily="Segoe UI" UseLayoutRounding="True">
  <Window.Resources>
    <Style TargetType="Button">
      <Setter Property="Foreground" Value="#F2EEEB"/>
      <Setter Property="Background" Value="#252A34"/>
      <Setter Property="BorderBrush" Value="#424956"/>
      <Setter Property="BorderThickness" Value="1"/>
      <Setter Property="Padding" Value="14,7"/>
      <Setter Property="FontSize" Value="14"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="Face" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                    Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}"
                    BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="5" Padding="{TemplateBinding Padding}">
              <ContentPresenter VerticalAlignment="Center" HorizontalAlignment="Center" RecognizesAccessKey="True"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Face" Property="BorderBrush" Value="#F3BA9E"/></Trigger>
              <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Face" Property="BorderBrush" Value="White"/></Trigger>
              <Trigger Property="IsPressed" Value="True"><Setter TargetName="Face" Property="Opacity" Value=".75"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>
  <Grid Margin="24,19,24,19">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <TextBlock Text="Game Library" FontSize="31" FontWeight="Bold" Margin="0,0,0,15"/>
    <ScrollViewer Name="GameScroll" Grid.Row="1" VerticalScrollBarVisibility="Auto">
      <StackPanel Name="Games"/>
    </ScrollViewer>
    <DockPanel Grid.Row="2" Margin="0,12,0,0">
      <Button Name="Back" Content="_Back" DockPanel.Dock="Right" IsCancel="True"/>
      <Button Name="Locate" Content="_Locate installed game..." DockPanel.Dock="Left"/>
      <TextBlock Name="Status" TextWrapping="Wrap" Foreground="#F3BA9E" VerticalAlignment="Center" Margin="10,0"/>
    </DockPanel>
  </Grid>
</Window>
"@
    $window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup))
    if ($Owner) { $window.Owner=$Owner }
    $workArea=[Windows.SystemParameters]::WorkArea
    $window.Width=[Math]::Min(690,$workArea.Width-32)
    $window.MaxHeight=$workArea.Height-24
    $games=$window.FindName('Games')
    $scroll=$window.FindName('GameScroll')
    $scroll.MaxHeight=[Math]::Max(120,$window.MaxHeight-164)
    $status=$window.FindName('Status')
    $state=@{ Selection=$null; Focus=$null }

    $refresh={
        $games.Children.Clear()
        $status.Text=''
        $state.Focus=$null
        foreach ($item in Get-HotdLibraryInstallations $RecompDir $CurrentGame) {
            $card=[Windows.Controls.Border]::new()
            $card.Background=[Windows.Media.BrushConverter]::new().ConvertFromString('#1D222B')
            $card.BorderBrush=[Windows.Media.BrushConverter]::new().ConvertFromString($item.Accent)
            $card.BorderThickness=[Windows.Thickness]::new(3,0,0,0)
            $card.CornerRadius=[Windows.CornerRadius]::new(6)
            $card.Padding=[Windows.Thickness]::new(12,11,11,11)
            $card.Margin=[Windows.Thickness]::new(0,0,3,7)
            $row=[Windows.Controls.DockPanel]::new()
            $actions=[Windows.Controls.StackPanel]::new()
            $actions.Orientation='Horizontal'
            $actions.VerticalAlignment='Center'
            [Windows.Controls.DockPanel]::SetDock($actions,'Right')
            [void]$row.Children.Add($actions)
            $title=[Windows.Controls.StackPanel]::new()
            $title.VerticalAlignment='Center'
            $title.Margin=[Windows.Thickness]::new(0,0,10,0)
            $name=[Windows.Controls.TextBlock]::new()
            $name.Text=$item.Title
            $name.FontSize=18
            $name.FontWeight='SemiBold'
            $name.TextWrapping='Wrap'
            [void]$title.Children.Add($name)
            if ($item.Game -eq $CurrentGame) {
                $current=[Windows.Controls.TextBlock]::new()
                $current.Text='Current game'
                $current.FontSize=12
                $current.Foreground=[Windows.Media.BrushConverter]::new().ConvertFromString($item.Accent)
                [void]$title.Children.Add($current)
            }
            [void]$row.Children.Add($title)
            $options=[ordered]@{}
            if ($item.Game -ne $CurrentGame) { $options.Menu='Menu' }
            if ($item.CanSettings) { $options.Settings='Settings' }
            if ($item.CanPlay) { $options.Play='Play' }
            if ($options.Count -eq 0) { $options.Menu='Menu' }

            foreach ($action in $options.Keys) {
                $button=[Windows.Controls.Button]::new()
                $button.Content=$options[$action]
                $button.Margin=[Windows.Thickness]::new(5,0,0,0)
                $button.Tag=@{ Installation=$item; Action=$action; State=$state; Window=$window }
                $button.ToolTip=switch ($action) {
                    Menu { 'Open the game menu' }
                    Settings { 'Open game settings' }
                    Play { 'Start a game using saved settings' }
                }
                [Windows.Automation.AutomationProperties]::SetName($button,("$($button.Content) - $($item.Title)"))
                if ($action -eq 'Play') {
                    $button.Background=[Windows.Media.BrushConverter]::new().ConvertFromString('#783B40')
                    $button.BorderBrush=[Windows.Media.BrushConverter]::new().ConvertFromString('#AA5D60')
                    if (!$state.Focus -or $item.Game -eq $CurrentGame) { $state.Focus=$button }
                }
                $button.Add_Click({
                    param($sender,$eventArgs)
                    $target=$sender.Tag
                    $target.State['Selection']=@{ Installation=$target.Installation; Action=$target.Action }
                    $target.Window.Close()
                })
                [void]$actions.Children.Add($button)
            }
            $card.Child=$row
            [void]$games.Children.Add($card)
        }
        if ($games.Children.Count -eq 0) { $status.Text='Locate a complete installation to add it.' }
    }.GetNewClosure()
    & $refresh

    $window.FindName('Locate').Add_Click({
        Add-Type -AssemblyName System.Windows.Forms
        $dialog=[Windows.Forms.FolderBrowserDialog]::new()
        $dialog.Description='Select the installation folder for HOTD4, HOTD4 Special, HOTD EX or Scarlet Dawn.'
        $dialog.ShowNewFolderButton=$false
        try {
            if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { return }
            $found=@(foreach ($game in Get-HotdLibraryGames) { Resolve-Hotd4Installation $dialog.SelectedPath $game })
            if (!$found) { throw 'No complete HOTD installation was found in that folder.' }
            foreach ($item in $found) { Register-Hotd4Installation $item.App $item.Game }
            $status.Text=''
            & $refresh
        } catch {
            $status.Text=$_.Exception.Message
        } finally { $dialog.Dispose() }
    }.GetNewClosure())
    $window.Add_ContentRendered({
        $focus=if ($state.Focus) { $state.Focus } else { $window.FindName('Locate') }
        [void]$focus.Focus()
    }.GetNewClosure())

    if ($Inspect) { & $Inspect $window $games; return $state.Selection }
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
