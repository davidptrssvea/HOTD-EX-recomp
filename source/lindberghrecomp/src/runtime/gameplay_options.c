#define _POSIX_C_SOURCE 200809L
#include "gameplay_options.h"
#include "atomic_file.h"
#include <stdlib.h>
#include <stdatomic.h>
static atomic_int offscreen_reload=1,blood_splatter=0;
static int free_play, mute_credit_sounds, arcade_options_loaded;
static void path_for(char *path,size_t size,const char *name)
{
    const char *dir=getenv("HOTD4_SETTINGS_DIR");
    if(dir && *dir)snprintf(path,size,"%s/%s",dir,name);
    else {
#ifdef _WIN32
        GetModuleFileNameA(NULL,path,(DWORD)size);
        char *slash=strrchr(path,'\\');
        if(slash)snprintf(slash+1,size-(size_t)(slash+1-path),"%s",name);
        else snprintf(path,size,"%s",name);
#else
        snprintf(path,size,"%s",name);
#endif
    }
}
void gameplay_options_set(int offscreen,int blood)
{ atomic_store(&offscreen_reload,!!offscreen);atomic_store(&blood_splatter,!!blood); }
int gameplay_offscreen_reload(void){return atomic_load(&offscreen_reload);}
int gameplay_blood_splatter(void){return atomic_load(&blood_splatter);}
static int read_arcade_toggle(const char *name)
{
    char path[2048];
    path_for(path,sizeof path,name);
    FILE *f=fopen(path,"rb");
    if (!f) return 0;
    int value=fgetc(f);
    fclose(f);
    return value=='1';
}

int gameplay_free_play(void) { return free_play; }
int gameplay_mute_credit_sounds(void) { return mute_credit_sounds; }

void gameplay_options_load(void)
{
    if (!arcade_options_loaded) {
        free_play=read_arcade_toggle("arcade-free-play.txt");
        mute_credit_sounds=read_arcade_toggle("arcade-mute-credit-sounds.txt");
        arcade_options_loaded=1;
    }
#ifndef LINDBERGH_OPENXR
    const char *names[]={"offscreen-reload.txt","blood-effects.txt"};
    int values[]={1,0};
    for(int i=0;i<2;i++){char path[2048],value[8];path_for(path,sizeof path,names[i]);FILE *f=fopen(path,"rb");
        if(f){if(fgets(value,sizeof value,f) && (value[0]=='0'||value[0]=='1'))values[i]=value[0]-'0';fclose(f);}}
    gameplay_options_set(values[0],values[1]);
#endif
}
void gameplay_options_save(void)
{
    const char *names[]={"offscreen-reload.txt","blood-effects.txt"};
    int values[]={gameplay_offscreen_reload(),gameplay_blood_splatter()};
    for(int i=0;i<2;i++){char path[2048];path_for(path,sizeof path,names[i]);LgAtomicFile f;
        if(lg_atomic_file_begin(&f,path)){if(fputc(values[i]?'1':'0',f.stream)==EOF)lg_atomic_file_abort(&f);
            else if(!lg_atomic_file_commit(&f))fprintf(stderr,"[settings] Cannot save %s\n",names[i]);}}
}
