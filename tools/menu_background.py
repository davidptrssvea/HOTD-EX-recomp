"""Extract EX's menu background only on the user's PC, from their SFD movie."""
import argparse,hashlib,json,subprocess,sys,tempfile
from pathlib import Path

def generate(app,dump,toolchain):
    app=Path(app);source=Path(dump)/'fs/sfd/title_bg.sfd';dest=app/'hotdex-menu.jpg'
    if dest.is_file() and dest.stat().st_size>1024:
        print('Keeping existing local menu background.',flush=True);return
    ffmpeg=Path(toolchain)/'mingw64/bin/ffmpeg.exe'
    if not source.is_file():raise ValueError('Missing original menu movie: '+str(source))
    with tempfile.TemporaryDirectory(prefix='hotdex-menu-') as td:
        output=Path(td)/'background.jpg'
        result=subprocess.run([str(ffmpeg),'-hide_banner','-nostdin','-loglevel','error','-ss','3','-i',str(source),'-frames:v','1','-q:v','2','-y',str(output)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        log=result.stdout.decode('utf-8','replace');(app/'menu-background.log').write_text(log,encoding='utf-8')
        if result.returncode or not output.is_file() or output.stat().st_size<10000:raise RuntimeError('Could not extract the local menu image: '+log)
        pending=app/'hotdex-menu.pending';pending.write_bytes(output.read_bytes());pending.replace(dest)
    (app/'menu-background-source.json').write_text(json.dumps({'generated_locally':True,'method':'frame extracted from own arcade movie','frame_time_seconds':3,'source':'fs/sfd/title_bg.sfd','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'image_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()},indent=2))
    print('Created EX menu background from your own dump.',flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--app',required=True);p.add_argument('--dump',required=True);p.add_argument('--toolchain',required=True);a=p.parse_args()
    try:generate(a.app,a.dump,a.toolchain)
    except Exception as e:print('Menu image extraction failed: '+str(e),file=sys.stderr);sys.exit(1)
