using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
class Launcher {
 [STAThread] static int Main(string[] args) {
  try {
   string root=AppDomain.CurrentDomain.BaseDirectory;
   bool direct=false,settings=false;
   foreach(string arg in args) {
    if(arg=="--no-launcher" || arg=="--skip-launcher") direct=true;
    else if(arg=="--settings") settings=true;
    else throw new ArgumentException("Unknown option: "+arg+"\nUse --no-launcher or --settings.");
   }
   string app=Path.Combine(root,"HOTDEX_Recomp");
   string script=Path.Combine(app,direct?"hotdex-launcher.ps1":"config_menu.ps1");
   if(!File.Exists(script)) throw new FileNotFoundException("Required launcher file is missing",script);
   var p=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe"));
   p.Arguments="-NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File \""+script+"\""+(!direct && settings?" -Page Settings":"");
   p.WorkingDirectory=app;p.UseShellExecute=false;p.CreateNoWindow=true;
   using(var child=Process.Start(p)){}
   return 0;
  } catch(Exception e) {MessageBox.Show(e.Message,"HOTD EX - Launch error",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}
 }
}
