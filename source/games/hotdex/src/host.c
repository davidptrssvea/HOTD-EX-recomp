/* The House of the Dead EX, using the shared Lindbergh runtime. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_profile.h"
#include "lindbergh_rt.h"
#include "skip_media.h"
#include "gameplay_options.h"

#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
#include "gl_posix.h"
#include "host_settings.h"
extern void host_use_guest_viewport_coords(void);
static HleHandler original_get_integer;
void L_082A2730(CPU *c);
static void h_scene_screen_size(CPU *c)
{
    /* CEnvMgr::InitGameScreenMag clamps the scene to the cabinet's screen
     * table even when GetRenderResolution requests more. Use the selected
     * size while it constructs the scene camera and its render surface,
     * then restore the table before any sprite layout is calculated. */
    unsigned mode=rd32(0x087aa07cu);
    if(mode>=26){fprintf(stderr,"[hotdex] invalid screen mode %u\n",mode);exit(1);}
    uint32_t entry=0x087859a0u+mode*24;
    uint32_t width=rd32(entry),height=rd32(entry+4);
    HostSettings *st=settings();
    wr32(entry,st->window_w);wr32(entry+4,st->window_h);
    push32(c,rd32(c->esp-4));L_082A2730(c);
    wr32(entry,width);wr32(entry+4,height);
}
static void h_render_resolution(CPU *c)
{
    /* This internal GCC entry takes the two output pointers in EAX/EDX.
     * The cabinet normally renders the scene at 850x480 independently of
     * its 1360x768 sprite canvas. Resize the scene target, not that canvas. */
    HostSettings *st=settings();
    unsigned w=st->window_w>0?(unsigned)st->window_w:1360;
    unsigned h=st->window_h>0?(unsigned)st->window_h:768;
    wr32(c->eax,w);wr32(c->edx,h);
    static int logged;
    if(!logged++)fprintf(stderr,"[hotdex] internal scene resolution: %ux%u (sprite canvas 1360x768)\n",w,h);
}
static void h_get_integer(CPU *c)
{
    unsigned name=A32(0);
    int *rect=APTR(1);
    original_get_integer(c);
    /* EX saves/restores viewport and scissor state around post-processing.
     * The guest must read cabinet coordinates, not the scaled window pixels;
     * otherwise each restore scales the rectangle again (up to 32768 at 4K).
     * The shared unscale helper leaves offscreen FBO coordinates untouched. */
    if(name==GL_VIEWPORT || name==GL_SCISSOR_BOX)host_viewport_unscale(rect);
}
static void install_desktop_layout(void)
{
    if(!guest_override("_Z19GetRenderResolutionPjS_.",h_render_resolution)){
        fprintf(stderr,"[hotdex] missing internal render resolution entry point\n");
        exit(1);
    }
    if(!guest_override("_ZN7CEnvMgr17InitGameScreenMagEv",h_scene_screen_size)){
        fprintf(stderr,"[hotdex] missing scene camera setup entry point\n");exit(1);
    }
    /* EX's legacy compositing path produces a black scene with an 8-sample
     * default framebuffer on the tested Windows driver. Keep the selectable
     * modes to the verified 0/2/4 samples until that path is repaired. */
    if(settings()->aa > 4){
        fprintf(stderr,"[hotdex] MSAA %dx is not supported by this EX build; using 4x\n",settings()->aa);
        settings()->aa=4;
    }
    for(unsigned i=0;i<HLE_COUNT;i++)if(g_hle_handlers[i] && !strcmp(hle_name((HleId)i),"glGetIntegerv")){
        original_get_integer=g_hle_handlers[i];g_hle_handlers[i]=h_get_integer;
        host_use_guest_viewport_coords();
    }
}
#endif

/* Normal gameplay reads the pointer through CInputJvs::GetPosition,
 * not the operator menu's GetAdjstGunPos helper. The original returns
 * floats in the cabinet's 1360x768 coordinate system. Replace calibration
 * and four-frame smoothing here, leaving trigger/pedal edges in the game. */
static void h_gun_position(CPU *c)
{
    unsigned player = A32(1) & 0xff;
    if (player >= 2 || !A32(2)) return;
    int x, y, w, h;
    if (player == 1) { if (!host_gun2_state(&x, &y)) x = y = -100000; }
    else host_mouse_state(&x, &y, NULL);
    if (host_cabinet_buttons() & (player ? CAB_P2_RELOAD : CAB_P1_RELOAD)) x = y = -100000;
    lindbergh_window_size(&w, &h);
    float position[2] = { x * (1360.0f / (w > 0 ? w : 1360)),
                          y * (768.0f / (h > 0 ? h : 768)) };
    memcpy(APTR(2), position, sizeof position);
}

static const LgGameProfile k_profile = {
    .title = "The House of the Dead EX",
    .width = 1360, .height = 768,
    .pace_fps = 60,
    .sysdata_region = 1,
    /* InputGetJvs: both pedals are on the first JVS player's byte 0.
     * The game maintains their press/held/release states itself. */
    .action_switch = {{0, 0, 0x08}, {0, 0, 0x04}},
};

static int ex_operator_available;
static unsigned ex_operator_button(unsigned buttons)
{
    static int held;int down=(buttons&CAB_TEST)!=0;
    if(down && !held){if(ex_operator_available){fprintf(stderr,"[hotdex] opening arcade Test Menu\n");exit(76);}fprintf(stderr,"[hotdex] Test Menu unavailable: install the verified hodextestR.elf with Setup\n");}
    held=down;return buttons&~CAB_TEST;
}

void sega_credit_options(uint32_t init, uint32_t config, uint32_t sound, uint32_t free_play_flag,
                         uint32_t text, uint32_t first, uint32_t last);

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <hodexRI.elf> [game args...]\n", argv[0]);
        return 2;
    }
    char operator_path[4096],app_path[4096];
    snprintf(operator_path,sizeof operator_path,"%s",argv[1]);
    char*a=strrchr(operator_path,'/'),*b=strrchr(operator_path,'\\'),*tail=a&&(!b||a>b)?a:b;
    if(tail)snprintf(tail+1,sizeof operator_path-(size_t)(tail+1-operator_path),"hodextestR.elf");else strcpy(operator_path,"hodextestR.elf");
    const char*dir=getenv("HOTD4_SETTINGS_DIR");
    snprintf(app_path,sizeof app_path,"%s/hotdex-testmenu.exe",dir&&*dir?dir:".");
    FILE*op=fopen(operator_path,"rb"),*exe=fopen(app_path,"rb");ex_operator_available=op&&exe;if(op)fclose(op);if(exe)fclose(exe);
    guest_install_crash_handler();
    if (guest_load(argv[1], argc - 1, argv + 1) != 0) return 1;
    game_profile_apply(&k_profile);
    hle_register_all();
    host_cabinet_button_filter(ex_operator_button);
    gameplay_options_load();
    sega_credit_options(0x084b572eu, 0x084b68acu, 0x084b6677u, 0x0aa3dd67u,
                        0x084b6767u, 0x080592e5u, 0x08059355u);
    install_media_skip();
#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
    install_desktop_layout();
#endif
    if (!guest_override("_ZN9CInputJvs11GetPositionEaP12KfVectorNNGL", h_gun_position)) {
        fprintf(stderr, "[hotdex] missing gun input entry point; unsupported executable\n");
        return 1;
    }
    CPU cpu;
    guest_init_cpu(&cpu);
    dispatch(&cpu, guest_entry());
    fprintf(stderr, "[host] the entry point returned - _start should not.\n");
    return 1;
}
