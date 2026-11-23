/* The arcade's own operator menu, rebuilt locally from hodextestR.elf. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_profile.h"
#include "gl_posix.h"
#include "lindbergh_rt.h"

extern unsigned g_frame;
static unsigned operator_buttons(unsigned buttons)
{
    /* Do not let the held Test key that launched this process immediately
     * select EXIT. Require release after the first quarter-second. */
    static int armed;
    if(!armed) {
        if(g_frame>=15 && !(buttons & (CAB_TEST|CAB_SERVICE)))armed=1;
        return buttons & ~(CAB_TEST|CAB_SERVICE);
    }
    return buttons;
}

static HleHandler original_clear;
static void operator_clear(CPU *c)
{
    /* Unlike the game, the boot test program relies on GL's initial viewport.
     * Refit it on each frame so resizing a 4:3 menu cannot stretch its text. */
    int x=0,y=0,w=640,h=480;
    host_viewport_scale(&x,&y,&w,&h);
    glViewport(x,y,w,h);
    original_clear(c);
}

static HleHandler original_fopen;
static void operator_fopen(CPU *c)
{
    const char *path=ASTR(0), *mode=ASTR(1);
    const char *prefix="/usr/lib/boot/";
    if (!strncmp(path,prefix,strlen(prefix)) && mode[0]=='r') {
        const char *name=path+strlen(prefix);
        if (!strcmp(name,"SEGA_KakuGothic-DB-Roman_12.tga") || !strcmp(name,"SEGA_KakuGothic-DB-Roman_12.abc") || !strcmp(name,"logo.tga")) {
            char local[2048];FILE *file=NULL;
            const char *dump=getenv("TEA_DIR"), *assets=getenv("HOTD4_OPERATOR_ASSETS");
            if(dump && *dump) {snprintf(local,sizeof local,"%s/elf/%s",dump,name);file=fopen(local,mode);}
            if(!file && assets && *assets) {snprintf(local,sizeof local,"%s/%s",assets,name);file=fopen(local,mode);}
            if(file) {fprintf(stderr,"[operator] resource %s\n",name);c->eax=guest_file_wrap(file);return;}
        }
    }
    original_fopen(c);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <hodextestR.elf> [test program arguments...]\n", argv[0]);
        return 2;
    }
    guest_install_crash_handler();
    if (guest_load(argv[1], argc - 1, argv + 1)) return 1;
    const LgGameProfile profile = {.title="The House of the Dead EX - Arcade Test Menu",
                                  .width=640, .height=480, .pace_fps=60, .sysdata_region=1};
    game_profile_apply(&profile);
    hle_register_all();
    host_cabinet_button_filter(operator_buttons);
    original_clear=g_hle_handlers[HLE_glClear];
    hle_bind("glClear",operator_clear);
    original_fopen=g_hle_handlers[HLE_fopen];
    hle_bind("fopen",operator_fopen);
    CPU cpu;
    guest_init_cpu(&cpu);
    dispatch(&cpu, guest_entry());
    return 0;
}
