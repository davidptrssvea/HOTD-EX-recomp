"""Generate and build HOTD EX locally from the user's verified arcade files."""
import argparse,hashlib,json,os,re,shutil,subprocess,sys,tempfile,time
from pathlib import Path
from build_support import run,build_jobs
ROOT=Path(__file__).resolve().parents[1]
SPEC=json.loads((ROOT/'tools/dump-files.json').read_text())

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def find_dump(path):
    p=Path(path).resolve()
    if p.is_file():p=p.parent
    candidates=[p,p/'disk0']
    if p.name.lower()=='elf':candidates.append(p.parent)
    for d in candidates:
        if not (d/'elf/hodexRI.elf').is_file():continue
        for name,expected in SPEC['files'].items():
            file=d/name
            if not file.is_file():raise ValueError('Incomplete dump: missing '+name)
            actual=digest(file)
            if actual!=expected:raise ValueError(f'Unsupported {name}: SHA-256 {actual}. Expected {expected}. The original dump was not modified.')
        for name in SPEC['required']:
            file=d/name
            if not file.exists() or (file.is_dir() and not any(x.is_file() for x in file.rglob('*'))):raise ValueError('Incomplete dump: missing '+name)
        return d
    raise ValueError('Cannot find elf/hodexRI.elf. Select your extracted HOTD EX disk0 folder or its parent.')

def atomic_copy(source,destination):
    destination=Path(destination);destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent,prefix='.hotdex-',delete=False) as f:stage=Path(f.name)
    try:shutil.copy2(source,stage);stage.replace(destination)
    finally:stage.unlink(missing_ok=True)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--dump',required=True);ap.add_argument('--output',required=True);ap.add_argument('--toolchain',required=True);ap.add_argument('--jobs',type=int,default=0);ap.add_argument('--validate-only',action='store_true');ap.add_argument('--stage-update',action='store_true');a=ap.parse_args()
    dump=find_dump(a.dump);print('Verified The House of the Dead EX:',dump,flush=True)
    if a.validate_only:return
    out=Path(a.output).resolve();tool=Path(a.toolchain).resolve();bin=tool/'mingw64/bin'
    for protected in (dump,ROOT/'source',ROOT/'tools',ROOT/'ui',tool):
        if out==protected or out in protected.parents or protected in out.parents:raise ValueError('Choose separate dump, builder, tools and installation folders.')
    for name in ('gcc.exe','cmake.exe','ninja.exe','python.exe','objdump.exe','strip.exe'):
        if not (bin/name).is_file():raise ValueError('Missing build tool: '+str(bin/name))
    os.environ['PATH']=str(bin)+os.pathsep+os.environ.get('PATH','');os.environ['CC']=str(bin/'gcc.exe')
    jobs=build_jobs(a.jobs);print(f'Compiler workers: {jobs} (Setup -Jobs N overrides automatic RAM/CPU limit).',flush=True)
    source=ROOT/'source';gen=source/'games/hotdex/generated'
    manifest=json.loads((ROOT/'package-manifest.json').read_text())
    inputs=[x for x in manifest['files'] if x['path'].startswith(('source/lindberghrecomp/tools/','source/lindberghrecomp/pcrecomp/tools/','tools/gl-','tools/dump-files.json'))]
    signature=hashlib.sha256(json.dumps(['hotdex-generator-v1',SPEC,inputs,digest(bin.parent/'include/GL/glew.h')],sort_keys=True).encode()).hexdigest();marker=gen/'local-build-signature.txt'
    if not marker.is_file() or marker.read_text()!=signature:
        with tempfile.TemporaryDirectory(dir=gen.parent,prefix='.generated-') as td:
            stage=Path(td)
            run([bin/'python.exe','-m','tools','recomp',dump/'elf/hodexRI.elf',stage],cwd=source/'lindberghrecomp')
            run([bin/'python.exe','-m','tools.recomp.solib',stage,'--main-elf',dump/'elf/hodexRI.elf',dump/'elf/librnalindbergh_jr.so',dump/'elf/libcri_soundoutput_lindbergh_jr.so'],cwd=source/'lindberghrecomp')
            thunk=stage/'recomp_gl_thunks_windows.c'
            with thunk.open('w',encoding='utf-8') as f:run([bin/'python.exe',source/'lindberghrecomp/tools/gen_gl_thunks.py',stage/'recomp_imports.h',bin.parent/'include/GL/glew.h'],stdout=f)
            text=thunk.read_text();text=text.replace('#include "arbprog.h"','#include "arbprog.h"\n'+(ROOT/'tools/gl-enhancements.h').read_text())
            for name,body in json.loads((ROOT/'tools/gl-overrides.json').read_text()).items():
                text,n=re.subn(r'static void th_'+name+r'\(CPU \*c\)\n\{.*?\n\}',lambda _:body,text,count=1,flags=re.S)
                if n!=1:raise ValueError('GL wrapper generation changed: '+name)
            thunk.write_text(text);(stage/'local-build-signature.txt').write_text(signature)
            gen.mkdir(exist_ok=True)
            if not gen.resolve().is_relative_to(ROOT.resolve()):raise ValueError('Generated-code folder must stay within this builder.')
            names={p.name for p in stage.iterdir()}
            for old in gen.iterdir():
                if old.is_file() and old.name.startswith('recomp_') and old.suffix in ('.c','.h') and old.name not in names:old.unlink()
            # A generator change need not force every large C unit to compile
            # again. Preserve timestamps for byte-identical outputs.
            for file in stage.iterdir():
                destination=gen/file.name
                if not destination.is_file() or digest(destination)!=digest(file):atomic_copy(file,destination)
    else:print('Reusing verified local generated code.',flush=True)
    build=ROOT/'build'
    run([bin/'cmake.exe','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DLINDBERGH_GAMES=hotdex','-DLINDBERGH_OPENXR=OFF','-DLINDBERGH_RENDERER=OPENGL','-DCMAKE_C_COMPILER='+str(bin/'gcc.exe'),'-DCMAKE_MAKE_PROGRAM='+str(bin/'ninja.exe'),'-DCMAKE_PREFIX_PATH='+str(bin.parent)])
    run([bin/'cmake.exe','--build',build,'--target','hotdex','--parallel',jobs])
    executable=build/'hotdex.exe';pending=[executable];dlls={};seen=set();system=Path(os.environ['SystemRoot'])/'System32'
    # Current MSYS2 can resolve SDL2 through sdl2-compat. SDL2.dll loads
    # SDL3.dll dynamically, so it is absent from its PE import table.
    if b'SDL3.dll' in (bin/'SDL2.dll').read_bytes():
        sdl3=bin/'SDL3.dll'
        if not sdl3.is_file():raise ValueError('SDL2 compatibility runtime needs SDL3.dll. Rerun Setup to repair the build tools.')
        dlls['SDL3.dll']=sdl3;pending.append(sdl3)
    while pending:
        text=subprocess.check_output([str(bin/'objdump.exe'),'-p',str(pending.pop())],text=True,errors='replace')
        for name in re.findall(r'DLL Name:\s*(\S+)',text):
            if name.lower() in seen:continue
            seen.add(name.lower());file=bin/name
            if file.is_file():dlls[name]=file;pending.append(file)
            elif not (system/name).is_file() and not name.lower().startswith(('api-ms-','ext-ms-')):raise ValueError('Unresolved runtime dependency: '+name)
    out.mkdir(parents=True,exist_ok=True);app=out/'HOTDEX_Recomp';app.mkdir(exist_ok=True)
    if not a.stage_update:
        localdump=out/'disk0';print('Copying your arcade data into the installation (the source remains unchanged)...',flush=True)
        shutil.copytree(dump/'fs',localdump/'fs',dirs_exist_ok=True)
        for name in SPEC['files']:atomic_copy(dump/name,localdump/name)
        # Seed a private cabinet save once; an update never overwrites it.
        if not (app/'lindbergh_nvram.bin').exists() and (dump/'elf/lindbergh_nvram.bin').is_file():shutil.copy2(dump/'elf/lindbergh_nvram.bin',app/'lindbergh_nvram.bin')
    for name,file in dlls.items():atomic_copy(file,app/name)
    atomic_copy(executable,app/'hotdex-desktop.exe');run([bin/'strip.exe',app/'hotdex-desktop.exe'])
    for file in (ROOT/'ui').iterdir():
        if file.name=='lindbergh.ini' and (app/file.name).exists():continue
        atomic_copy(file,app/file.name)
    compiler=Path(os.environ['SystemRoot'])/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    run([compiler,'/nologo','/target:winexe','/platform:x64','/reference:System.Windows.Forms.dll','/out:'+str(out/'Play HOTD EX.exe').replace('/','\\'),str(ROOT/'tools/windows-launcher.cs').replace('/','\\')])
    for name in ('Play HOTD EX.cmd','Play HOTD EX (Direct).cmd','Settings.cmd'):
        atomic_copy(ROOT/'tools'/name,out/name)
    licenses=app/'licenses';licenses.mkdir(exist_ok=True)
    for label,path in [('ports',source/'LICENSE'),('lindberghrecomp',source/'lindberghrecomp/LICENSE'),('pcrecomp',source/'lindberghrecomp/pcrecomp/LICENSE')]:atomic_copy(path,licenses/(label+'.txt'))
    for directory in (bin.parent/'share/licenses').glob('*'):
        if directory.is_dir():shutil.copytree(directory,licenses/'dependencies'/directory.name,dirs_exist_ok=True)
    atomic_copy(ROOT/'README.md',out/'README.md')
    (out/'local-build.json').write_text(json.dumps({'built_locally':True,'elf_sha256':SPEC['files']['elf/hodexRI.elf'],'generator_signature':signature,'package_manifest_sha256':digest(ROOT/'package-manifest.json'),'executable_sha256':digest(app/'hotdex-desktop.exe'),'built_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())},indent=2))
    print('Build complete: '+str(out/'Play HOTD EX.exe'),flush=True)
if __name__=='__main__':
    try:main()
    except Exception as e:print('Setup failed: '+str(e),file=sys.stderr);sys.exit(1)
