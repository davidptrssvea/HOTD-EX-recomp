$script:HotdexControlRows=@(
 @('coin1','Add Coin (P1)',53),@('start1','Start (P1)',49),@('trigger','Shoot (P1 / primary mouse)',-1),@('reload','Reload (P1 / primary mouse)',-3),@('action','Pedal / Action (P1 / primary mouse)',32),
 @('firemode','Fire Mode',0),@('coin2','Add Coin (P2)',54),@('start2','Start (P2)',50),@('test','Arcade Test Menu',116),@('service','Service / Test Menu navigation',115),@('mute','Mute',109),
 @('pause','Pause',27),@('restart','Restart Arcade',1073741887),@('fullscreen','Toggle Fullscreen',1073741892),@('crosshair_toggle','Toggle Extra Crosshair',1073741883),@('mouse_lock','Mouse Lock',1073741882),
 @('trigger2','Shoot (P2 keyboard)',1073741922),@('reload2','Reload (P2 keyboard)',1073741923),@('action2','Pedal / Action (P2 keyboard)',1073742052)
)
function Get-HotdexBindings([string]$Dir) {
 $b=@{};foreach($row in $script:HotdexControlRows){$b[$row[0]]=[int]$row[2]}
 $ini=Join-Path $Dir 'lindbergh.ini'
 if([IO.File]::Exists($ini)){foreach($line in [IO.File]::ReadAllLines($ini)){
  if($line -match '^\s*(\w+)\s*=\s*([^#]+)'){$k=$Matches[1];$v=$Matches[2].Trim();if($b.ContainsKey($k)){
   if($v -match '^mouse([1-5])$'){$b[$k]=-[int]$Matches[1]}elseif($v -match '^-?\d+$'){$b[$k]=[int]$v}elseif($v.Length -eq 1){$b[$k]=[int][char]$v.ToLowerInvariant()}elseif($v -eq 'space'){$b[$k]=32}elseif($v -eq 'enter'){$b[$k]=13}
  }}
 }}
 $profile=Join-Path $Dir 'controls-profile.txt'
 if([IO.File]::Exists($profile)){foreach($line in [IO.File]::ReadAllLines($profile)){if($line -match '^(\w+)\s+(-?\d+)$' -and $b.ContainsKey($Matches[1])){$b[$Matches[1]]=[int]$Matches[2]}}}
 return $b
}
function Get-HotdexKeyName([int]$Code) {
 $special=@{0='Unassigned';-1='Left mouse';-2='Middle mouse';-3='Right mouse';-4='Mouse X1';-5='Mouse X2';8='Backspace';9='Tab';13='Enter';27='Esc';32='Space';127='Delete';1073741897='Insert';1073741898='Home';1073741899='Page Up';1073741901='End';1073741902='Page Down';1073741903='Right';1073741904='Left';1073741905='Down';1073741906='Up';1073741908='Numpad /';1073741909='Numpad *';1073741910='Numpad -';1073741911='Numpad +';1073741912='Numpad Enter';1073741922='Numpad 0';1073741923='Numpad .';1073742048='Left Ctrl';1073742049='Left Shift';1073742050='Left Alt';1073742052='Right Ctrl';1073742053='Right Shift';1073742054='Right Alt'}
 if($special.ContainsKey($Code)){return $special[$Code]}
 if($Code -ge 1073741882 -and $Code -le 1073741893){return 'F'+($Code-1073741881)}
 if($Code -ge 1073741913 -and $Code -le 1073741921){return 'Numpad '+($Code-1073741912)}
 if($Code -ge 33 -and $Code -le 126){return ([char]$Code).ToString().ToUpperInvariant()}
 if($Code -ge 160 -and $Code -le 65535 -and ![char]::IsControl([char]$Code)){return ([char]$Code).ToString().ToUpperInvariant()}
 return 'Key '+$Code
}
function ConvertTo-HotdexKey($Event) {
 $key=if($Event.Key -eq 'System'){$Event.SystemKey}else{$Event.Key};$vk=[Windows.Input.KeyInterop]::VirtualKeyFromKey($key)
 if($vk -ge 65 -and $vk -le 90){return $vk+32};if($vk -ge 48 -and $vk -le 57){return $vk}
 if($vk -ge 112 -and $vk -le 123){return 1073741882+$vk-112}
 if($vk -ge 97 -and $vk -le 105){return 1073741913+$vk-97}
 if($vk -ge 186 -and $vk -le 226){$character=[HotdexPlatform]::KeyCharacter($vk);if($character){return $character}}
 $map=@{8=8;9=9;13=13;27=27;32=32;33=1073741899;34=1073741902;35=1073741901;36=1073741898;37=1073741904;38=1073741906;39=1073741903;40=1073741905;45=1073741897;46=127;96=1073741922;106=1073741909;107=1073741911;109=1073741910;110=1073741923;111=1073741908;160=1073742049;161=1073742053;162=1073742048;163=1073742052;164=1073742050;165=1073742054;186=59;187=61;188=44;189=45;190=46;191=47;192=96;219=91;220=92;221=93;222=39}
 if($map.ContainsKey($vk)){return [int]$map[$vk]};return 0
}
function Write-HotdexText([string]$Path,[string]$Value){
 $temp=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
 try{[IO.File]::WriteAllText($temp,$Value,[Text.UTF8Encoding]::new($false));if([IO.File]::Exists($Path)){[IO.File]::Replace($temp,$Path,[NullString]::Value)}else{[IO.File]::Move($temp,$Path)}}finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Get-HotdexInputPreferences([string]$Dir,$Settings){
 $v=@{Mode=[int]$Settings.raw_mouse;Gun1='auto';Gun2='disabled';Border=0;Width=50;VSync=0}
 foreach($entry in @(@('Mode','input-profile.txt'),@('Gun1','raw-gun1.txt'),@('Gun2','raw-gun2.txt'),@('Border','sinden-profile.txt'),@('Width','sinden-width.txt'),@('VSync','vsync-profile.txt'))){$p=Join-Path $Dir $entry[1];if([IO.File]::Exists($p)){$v[$entry[0]]=[IO.File]::ReadAllText($p).Trim()}}
 if([string]$v.VSync -notmatch '^[01]$'){$v.VSync=0};if([string]$v.Mode -notmatch '^[0-5]$'){$v.Mode=0};if($v.Width -notin 25,50,75,100){$v.Width=50};return $v
}
function Write-HotdexInputPreferences([string]$Dir,$Values,$Bindings){
 foreach($entry in @(@('Mode','input-profile.txt'),@('Gun1','raw-gun1.txt'),@('Gun2','raw-gun2.txt'),@('Border','sinden-profile.txt'),@('Width','sinden-width.txt'),@('VSync','vsync-profile.txt'),@('AA','aa-profile.txt'),@('Offscreen','offscreen-reload.txt'))){Write-HotdexText (Join-Path $Dir $entry[1]) ([string]$Values[$entry[0]])}
 Write-HotdexText (Join-Path $Dir 'mouse-speed.txt') ([Convert]::ToString($Values.Speed,[Globalization.CultureInfo]::InvariantCulture))
 $lines=@('HOTD4-CONTROLS-1');foreach($row in $script:HotdexControlRows){$lines+=($row[0]+' '+$Bindings[$row[0]])};Write-HotdexText (Join-Path $Dir 'controls-profile.txt') (($lines -join "`n")+"`n")
}
function Initialize-HotdexPlatform {
 if('HotdexPlatform' -as [type]){return}
 Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HotdexPlatform {
 [StructLayout(LayoutKind.Sequential)] public struct Device {public IntPtr Handle;public uint Type;}
 [StructLayout(LayoutKind.Sequential)] struct Rect {public int L,T,R,B;}
 [StructLayout(LayoutKind.Sequential)] struct Point {public int X,Y;}
 [DllImport("user32.dll")] static extern uint GetRawInputDeviceList([Out] Device[] list,ref uint count,uint size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern uint GetRawInputDeviceInfo(IntPtr device,uint command,System.Text.StringBuilder name,ref uint size);
 [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window,out Rect r);
 [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window,ref Point p);
 [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
 [DllImport("user32.dll")] static extern IntPtr GetKeyboardLayout(uint thread);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern uint MapVirtualKeyEx(uint code,uint type,IntPtr layout);
 public static int KeyCharacter(int key){uint ch=MapVirtualKeyEx((uint)key,2,GetKeyboardLayout(0))&0x7FFFFFFF;return ch>0 && ch<=65535?char.ToLowerInvariant((char)ch):0;}
 public static string[] Devices(){uint count=0;uint size=(uint)Marshal.SizeOf(typeof(Device));if(GetRawInputDeviceList(null,ref count,size)==uint.MaxValue)return new string[0];var list=new Device[count];uint n=GetRawInputDeviceList(list,ref count,size);var paths=new System.Collections.Generic.List<string>();if(n==uint.MaxValue)return paths.ToArray();foreach(var d in list){if(d.Type!=0)continue;uint len=1024;var name=new System.Text.StringBuilder(1024);if(GetRawInputDeviceInfo(d.Handle,0x20000007,name,ref len)!=uint.MaxValue)paths.Add(name.ToString());}return paths.ToArray();}
 public static double[] Bounds(IntPtr window){Rect r;Point p=new Point();if(!GetClientRect(window,out r)||!ClientToScreen(window,ref p))return null;double scale=GetDpiForWindow(window)/96.0;if(scale<=0)scale=1;return new[]{p.X/scale,p.Y/scale,(r.R-r.L)/scale,(r.B-r.T)/scale};}
}
'@
}
