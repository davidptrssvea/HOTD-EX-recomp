#include "frame_timing.h"
#include "gameplay_options.h"
#include <pthread.h>
#include "mouse_shake.h"
/*
 * host_sdl.c - the host window on a POSIX host, through SDL2.
 *
 * The Windows build has this in hle_window.c, around a Win32 window and WGL.
 * The contract is the same: one window, one GL context, an input queue the
 * GLUT seam drains, the cabinet's switches and gun, and host_swap() at the end
 * of every frame. Keys are reported as Win32 virtual-key codes so the GLUT
 * seam is shared unchanged.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>

/* A bare executable (not a .app) started from a script is not made the active
 * application, so its window shows but keystrokes keep going to the terminal.
 * Register as a regular app and take focus. */
void host_menu_install(void);    /* host_menu_mac.m */

static void mac_activate(void)
{
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"),
                                                sel_registerName("sharedApplication"));
    ((void (*)(id, SEL, long))objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 0);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(app, sel_registerName("activateIgnoringOtherApps:"), YES);
}
#endif

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#endif
#include "lindbergh_rt.h"
#include "gl_posix.h"
#include "host_settings.h"
#include "game_profile.h"
#include "high_scores.h"
static HighScoreReader high_score_reader;
void host_high_scores_register(HighScoreReader reader){high_score_reader=reader;}
#ifdef LINDBERGH_VULKAN
#include "vulkan_host.h"
#include "host_stats.h"
#endif

#if defined(LINDBERGH_OPENXR)
/* Shared VR frame loop; Android uses gl4es, Windows uses desktop GL. */
#ifdef LINDBERGH_GL4ES
#include <gl4esinit.h>
#endif
int  xr_init(void);
void xr_frame_begin(void);
void xr_frame_end(void);

#ifdef LINDBERGH_GL4ES
static void APIENTRY_GL4ES main_fb_size(int *w, int *h) { *w = 640; *h = 480; }
#endif
#endif
#include "stereo.h"

static int crosshair_drawing_player;
static int g_guest_viewport_coords;
void host_use_guest_viewport_coords(void){g_guest_viewport_coords=1;}
static SDL_atomic_t sp_practice_ready;
void host_special_practice_ready(void){SDL_AtomicSet(&sp_practice_ready,1);}
static int hotd4_special_menu(void){const char *v=getenv("HOTD4SP_MENU");return v && *v=='1';}

#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
#define HOTD4_DESKTOP_PAUSE 1
#include "hotd4_controls.h"
static int g_pause_requested;
static void sinden_border_draw(int width, int height);
static int extra_crosshair_draw(float x, float y, float scale);
static int crosshair_size_get(void);
static void crosshair_color_apply(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
static int hotd4_pause_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("HOTD4_PAUSE_MENU");
        enabled = value && !strcmp(value, "1");
    }
    return enabled;
}
#endif

#ifndef HOTD4_DESKTOP_PAUSE
enum {HC_PAUSE,HC_RESTART,HC_FULLSCREEN,HC_CROSSHAIR,HC_MOUSELOCK};
#define hc_custom 0
#define hc_init() ((void)0)
static int hc_matches(int id,int key){return id==HC_PAUSE?key==SDLK_ESCAPE:id==HC_FULLSCREEN?key==SDLK_F11:id==HC_CROSSHAIR?key==SDLK_F2:id==HC_MOUSELOCK?key==SDLK_F1:0;}
#endif

#define DEFAULT_W 640
#define DEFAULT_H 480

unsigned g_frame;

/* w, h: the size the game draws at - its monitor, which is what the DIP
 * switches, the gun and every game-facing query report. The window on the
 * Mac's screen is a separate thing, any size: the drawable stays w x h and
 * the system scales it to the window (host_mac_fix_surface). */
static struct {
    SDL_Window   *win;
    SDL_GLContext gl;
    int           w, h;
} g_win;

#if defined(__APPLE__)
void host_mac_fix_surface(SDL_Window *win, int w, int h);   /* host_menu_mac.m */
#endif

static int g_vp_x = 0, g_vp_y = 0, g_vp_w = 0, g_vp_h = 0;
static float g_raw_x = -1, g_raw_y = -1;
#ifdef HOTD4_DESKTOP_PAUSE
static float g_raw_filtered_x, g_raw_filtered_y;
static Uint64 g_raw_update_tick;
#endif
static int g_relative_mode = 0;

/* A pointer position in the window, in the game's pixels. */
/* The picture the window shows: the game's own frame, or the widescreen
 * image (stereo_wide) whose middle k of the width is the game's screen. */
static void picture_size(int *w, int *h)
{
    if (!stereo_wide(w, h, NULL)) { *w = g_win.w; *h = g_win.h; }
}

typedef struct { int x, y, w, h; } GameViewport;

static GameViewport fit_game_viewport(int draw_w, int draw_h)
{
    GameViewport viewport = {0, 0, draw_w, draw_h};
    float aspect = 16.0f / 9.0f;
#if !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
    /* The standalone desktop operator program has a native 4:3 canvas. */
    int picture_w=0,picture_h=0;
    picture_size(&picture_w,&picture_h);
    if(picture_w>0 && picture_h>0)aspect=(float)picture_w/picture_h;
#endif
    const float drawable_aspect = (float)draw_w / draw_h;
    if (fabsf(drawable_aspect - aspect) > 0.01f) {
        if (drawable_aspect > aspect) viewport.w = (int)(draw_h * aspect + 0.5f);
        else viewport.h = (int)(draw_w / aspect + 0.5f);
        viewport.x = (draw_w - viewport.w) / 2;
        viewport.y = (draw_h - viewport.h) / 2;
    }
    return viewport;
}

static void to_game(int *x, int *y)
{
    int win_w = 0, win_h = 0, draw_w = 0, draw_h = 0;
    if (g_win.win) {
        SDL_GetWindowSize(g_win.win, &win_w, &win_h);
        SDL_GL_GetDrawableSize(g_win.win, &draw_w, &draw_h);
    }
    int px = *x, py = *y;
    if (win_w > 0 && win_h > 0 && draw_w > 0 && draw_h > 0 && (win_w != draw_w || win_h != draw_h)) {
        px = (int)((long)px * draw_w / win_w);
        py = (int)((long)py * draw_h / win_h);
    }
    if (g_vp_w > 0 && g_vp_h > 0 && g_win.w > 0 && g_win.h > 0) {
        // SDL events can arrive before the next GL viewport after a resize.
        GameViewport viewport = {g_vp_x, g_vp_y, g_vp_w, g_vp_h};
        if (draw_w > 0 && draw_h > 0) viewport = fit_game_viewport(draw_w, draw_h);
        int top = draw_h > 0 ? draw_h - viewport.y - viewport.h : viewport.y;
        long gx = ((long)(px - viewport.x) * g_win.w) / viewport.w;
        long gy = ((long)(py - top) * g_win.h) / viewport.h;
        *x = (int)(gx < 0 ? 0 : gx >= g_win.w ? g_win.w - 1 : gx);
        *y = (int)(gy < 0 ? 0 : gy >= g_win.h ? g_win.h - 1 : gy);
        return;
    }
    int ww = 0, wh = 0;
    float k;
    if (g_win.win) SDL_GetWindowSize(g_win.win, &ww, &wh);
    if (ww > 0 && wh > 0 && g_win.w > 0 && stereo_wide(NULL, NULL, &k)) {
        /* the side strips are beyond the game's screen: kept to its edge,
         * not off it, which is a reload */
        double u = ((double)*x / ww - 0.5) / k + 0.5;
        int gx = (int)(u * g_win.w);
        *x = gx < 0 ? 0 : gx >= g_win.w ? g_win.w - 1 : gx;
        *y = (int)((long)*y * g_win.h / wh);
        return;
    }
    if (ww > 0 && wh > 0 && g_win.w > 0) {
        *x = (int)((long)*x * g_win.w / ww);
        *y = (int)((long)*y * g_win.h / wh);
    }
}

static void to_window(int *x, int *y)
{
    if (g_vp_w > 0 && g_vp_h > 0 && g_win.w > 0 && g_win.h > 0) {
        int win_w = 0, win_h = 0, draw_w = 0, draw_h = 0;
        if (g_win.win) {
            SDL_GetWindowSize(g_win.win, &win_w, &win_h);
            SDL_GL_GetDrawableSize(g_win.win, &draw_w, &draw_h);
        }
        GameViewport viewport = {g_vp_x, g_vp_y, g_vp_w, g_vp_h};
        if (draw_w > 0 && draw_h > 0) viewport = fit_game_viewport(draw_w, draw_h);
        int top = draw_h > 0 ? draw_h - viewport.y - viewport.h : viewport.y;
        int px = viewport.x + (int)((long)*x * viewport.w / g_win.w);
        int py = top + (int)((long)*y * viewport.h / g_win.h);
        if (win_w > 0 && win_h > 0 && draw_w > 0 && draw_h > 0 && (win_w != draw_w || win_h != draw_h)) {
            px = (int)((long)px * win_w / draw_w);
            py = (int)((long)py * win_h / draw_h);
        }
        *x = px;
        *y = py;
        return;
    }
    int ww = 0, wh = 0;
    float k;
    if (g_win.win) SDL_GetWindowSize(g_win.win, &ww, &wh);
    if (ww > 0 && wh > 0 && g_win.w > 0 && stereo_wide(NULL, NULL, &k)) {
        *x = (int)((((double)*x / g_win.w - 0.5) * k + 0.5) * ww);
        *y = (int)((long)*y * wh / g_win.h);
        return;
    }
    if (ww > 0 && wh > 0 && g_win.w > 0) {
        *x = (int)((long)*x * ww / g_win.w);
        *y = (int)((long)*y * wh / g_win.h);
    }
}

static int env_int(const char *name, int dflt)
{
    const char *v = getenv(name);
    return (v && *v) ? atoi(v) : dflt;
}

static int g_hint_w, g_hint_h;     /* host_window_hint, before the window exists */

void lindbergh_window_size(int *w, int *h)
{
    /* before the window opens: what it will open at, hint included */
    if (w) *w = g_win.w ? g_win.w : env_int("LINDBERGH_WIDTH", g_hint_w ? g_hint_w : DEFAULT_W);
    if (h) *h = g_win.h ? g_win.h : env_int("LINDBERGH_HEIGHT", g_hint_h ? g_hint_h : DEFAULT_H);
}

/* ---- input queue ---- */
#define INQ 256
static HostInput g_inq[INQ];
static unsigned  g_in_head, g_in_tail;
static int       g_mouse_x, g_mouse_y, g_buttons;

static void in_push(int kind, int a, int b, int cc, int d)
{
    unsigned next = (g_in_head + 1) % INQ;
    if (next == g_in_tail) return;
    HostInput *e = &g_inq[g_in_head];
    e->kind = kind; e->a = a; e->b = b; e->c = cc; e->d = d;
    g_in_head = next;
}

int host_input_pop(HostInput *out)
{
    if (g_in_tail == g_in_head) return 0;
    *out = g_inq[g_in_tail];
    g_in_tail = (g_in_tail + 1) % INQ;
    return 1;
}

void host_mouse_state(int *x, int *y, int *buttons)
{
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
    if (buttons) *buttons = g_buttons;
}

/* ---- the cabinet ----
 * Every key and mouse button goes through the bindings in host_settings.c;
 * what each action does to the cabinet is here. */
static CabinetInput g_cab;
static unsigned g_held;       /* cabinet buttons held down right now */
/* Presses not yet seen by the I/O board. Events are drained once a frame and
 * the board is read once a frame, so a quick tap can go down and up inside
 * one drain; latching it until the next read means no tap is lost. */
static unsigned g_cab_latch;
/* LINDBERGH_LATENCY: when the trigger went down, until the board reports it */
static Uint64 g_lat_t; static unsigned g_lat_f;
static unsigned g_auto;       /* buttons driven by LINDBERGH_AUTOSTART/AUTOGUN */
static int      g_scripted;   /* the gun is scripted: real input is ignored */
static int      g_gun2_on, g_gun2_px = -100000, g_gun2_py = -100000;   /* the second gun (host_vr_input2, LINDBERGH_P2_TEST) */

#ifdef HOTD4_DESKTOP_PAUSE
#include "input_options.h"
int host_hotd4_restart_key(int key){return !raw_suspended && hc_matches(HC_RESTART,key);}
int host_hotd4_start_binding(void){return hc_get(ACT_START1);}
#else
#define input_active_mode 0
#define raw_held 0u
#define remote_held 0u
#endif

static pthread_mutex_t g_mouse_shake_mutex = PTHREAD_MUTEX_INITIALIZER;
static MouseShake g_mouse_shake;
static int g_mouse_shake_active;
static unsigned (*g_cab_button_filter)(unsigned);
void host_cabinet_button_filter(unsigned (*filter)(unsigned)) { g_cab_button_filter = filter; }

int host_mouse_shake(int active)
{
    pthread_mutex_lock(&g_mouse_shake_mutex);
    int pulse;
    if (input_active_mode >= 2) {
        g_mouse_shake_active = 0;
        mouse_shake_reset(&g_mouse_shake);
        pulse = -1;
    } else {
#ifdef HOTD4_DESKTOP_PAUSE
        if (raw_suspended) active = 0;
#endif
        if (!active || !g_mouse_shake_active) mouse_shake_reset(&g_mouse_shake);
        g_mouse_shake_active = active != 0;
        pulse = active && mouse_shake_pulse(&g_mouse_shake, SDL_GetTicks());
    }
    pthread_mutex_unlock(&g_mouse_shake_mutex);
    return pulse;
}

int host_gun2_state(int *x, int *y)
{
    if (x) *x = g_gun2_px;
    if (y) *y = g_gun2_py;
    return g_gun2_on;
}


static int g_capture = -1;    /* binding the next key or click, for this action */
static int g_calib;           /* aim calibration step, 0 = off */
static double g_calib_sx, g_calib_sy;     /* where the sight was clicked, step 1 */
static int g_pointer_in = 1;

static const unsigned CAB_OF[ACT_COUNT] = {
    [ACT_START1] = CAB_P1_START, [ACT_START2] = CAB_P2_START,
    [ACT_TRIGGER] = CAB_P1_TRIGGER, [ACT_RELOAD] = CAB_P1_RELOAD, [ACT_ACTION] = CAB_P1_ACTION,
    [ACT_FIREMODE] = CAB_P1_FIREMODE,
    [ACT_TEST] = CAB_TEST, [ACT_SERVICE] = CAB_SERVICE,
    [ACT_UP] = CAB_P1_UP, [ACT_DOWN] = CAB_P1_DOWN, [ACT_LEFT] = CAB_P1_LEFT, [ACT_RIGHT] = CAB_P1_RIGHT,
    [ACT_BUTTON1] = CAB_P1_TRIGGER, [ACT_BUTTON2] = CAB_P1_BUTTON2, [ACT_BUTTON3] = CAB_P1_BUTTON3,
};

/* ---- game controllers: the first one is player 1, the second player 2 ----
 * For a lever cabinet (Virtua Tennis 3): the d-pad or left stick is the
 * 8-way lever, A/B/X are buttons 1-3, Y is button 4, Start is start and Back
 * a coin. A gun game reads the same switches, so they do no harm there. */
static SDL_GameController *g_pad[2];
static unsigned g_pad_held;   /* both pads, as cabinet bits */

static unsigned pad_bits(SDL_GameController *p, int *coin)
{
    unsigned b = 0;
    int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
    int ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
    /* 8 sectors past a dead zone: an axis counts once it is over
     * tan(22.5 deg) of the other, so diagonals are as wide as the rest */
    if ((long)ax * ax + (long)ay * ay > 9000L * 9000L) {
        int fx = abs(ax) * 1000 > abs(ay) * 414, fy = abs(ay) * 1000 > abs(ax) * 414;
        if (fx) b |= ax < 0 ? CAB_P1_LEFT : CAB_P1_RIGHT;
        if (fy) b |= ay < 0 ? CAB_P1_UP : CAB_P1_DOWN;
    }
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= CAB_P1_UP;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= CAB_P1_DOWN;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= CAB_P1_LEFT;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= CAB_P1_RIGHT;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A)) b |= CAB_P1_TRIGGER;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B)) b |= CAB_P1_BUTTON2;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X)) b |= CAB_P1_BUTTON3;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_Y)) b |= CAB_P1_FIREMODE;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_START)) b |= CAB_P1_START;
    *coin = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK);
    return b;
}

/* player 1's bits -> the same switches of player 2 */
static unsigned as_p2(unsigned b)
{
    const unsigned gun = CAB_P1_TRIGGER | CAB_P1_RELOAD | CAB_P1_ACTION | CAB_P1_FIREMODE;
    const unsigned joy = CAB_P1_UP | CAB_P1_DOWN | CAB_P1_LEFT | CAB_P1_RIGHT | CAB_P1_BUTTON2 | CAB_P1_BUTTON3;
    return (b & CAB_P1_START ? CAB_P2_START : 0) | (b & gun) << CAB_P2_SHIFT | (b & joy) << CAB_P2_JOY_SHIFT;
}

static void pad_event(const SDL_Event *e)
{
    if (e->type == SDL_CONTROLLERDEVICEADDED) {
        for (int i = 0; i < 2; i++) {
            if (g_pad[i]) continue;
            g_pad[i] = SDL_GameControllerOpen(e->cdevice.which);
            if (g_pad[i]) fprintf(stderr, "[pad] player %d: %s\n", i + 1, SDL_GameControllerName(g_pad[i]));
            return;
        }
    } else if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        for (int i = 0; i < 2; i++) {
            if (!g_pad[i]) continue;
            SDL_Joystick *j = SDL_GameControllerGetJoystick(g_pad[i]);
            if (j && SDL_JoystickInstanceID(j) == e->cdevice.which) {
                fprintf(stderr, "[pad] player %d's controller removed\n", i + 1);
                SDL_GameControllerClose(g_pad[i]);
                g_pad[i] = NULL;
            }
        }
    }
}

static void pad_poll(void)
{
    static int coin_was[2];
    unsigned held = 0;
    for (int i = 0; i < 2; i++) {
        int coin = 0;
        if (g_pad[i]) {
            unsigned b = pad_bits(g_pad[i], &coin);
            held |= i ? as_p2(b) : b;
        }
        if (coin && !coin_was[i]) g_cab.coins[i]++;
        coin_was[i] = coin;
    }
    g_cab_latch |= held & ~g_pad_held;
    g_pad_held = held;
}

static void set_title(const char *msg);
static int g_capture_or_calib(void);

static void cabinet_action(HostAction a, int down)
{
    switch (a) {
    case ACT_COIN1:
        if (down) {
            g_cab.coins[0]++;
            fprintf(stderr, "[cab] Coin 1 inserted (total %u)\n", g_cab.coins[0]);
        }
        return;
    case ACT_COIN2:
        if (down) {
            g_cab.coins[1]++;
            fprintf(stderr, "[cab] Coin 2 inserted (total %u)\n", g_cab.coins[1]);
        }
        return;
    case ACT_MUTE:  if (down) host_audio_toggle_mute(); return;
    case ACT_START1:
        if (down && g_cab.coins[0] < 2) {
            g_cab.coins[0] += 2;
            fprintf(stderr, "[cab] Auto-inserted 2 coins on Start (total %u)\n", g_cab.coins[0]);
        }
        break;
    default: break;
    }
    unsigned bit = CAB_OF[a];
    /* The host gun supplies P1 directly; shared keyboard firing keys belong to P2. */
    if (input_active_mode >= 3 && a >= ACT_TRIGGER && a <= ACT_FIREMODE) bit <<= CAB_P2_SHIFT;
    if (!bit) return;
    if (down) { g_held |= bit; g_cab_latch |= bit; } else g_held &= ~bit;
    if (down && bit == CAB_P1_TRIGGER && !g_lat_t) { g_lat_t = SDL_GetPerformanceCounter(); g_lat_f = g_frame; }
}

/* A key (SDL keycode) or mouse button (MOUSE_BIND) went down or up. Returns
 * whether something was bound to it. */
static int cabinet_input(int bind, int down)
{
    HostSettings *st = settings();
#ifdef HOTD4_DESKTOP_PAUSE
    hc_init();
#endif
    int hit = 0;
    int actions=ACT_COUNT;
#ifdef HOTD4_DESKTOP_PAUSE
    actions=11;
#endif
    for (int a = 0; a < actions; a++)
        if (st->bind[a] == bind) { cabinet_action((HostAction)a, down); hit = 1; }
#ifdef HOTD4_DESKTOP_PAUSE
    if(!hc_custom && !hit){
#endif
    if (bind == SDLK_KP_5 || bind == 'c' || bind == 'C') { cabinet_action(ACT_COIN1, down); hit = 1; }
    if (bind == SDLK_KP_1 || bind == SDLK_RETURN || bind == SDLK_KP_ENTER) { cabinet_action(ACT_START1, down); hit = 1; }
    if (bind == SDLK_KP_6) { cabinet_action(ACT_COIN2, down); hit = 1; }
    if (bind == SDLK_KP_2) { cabinet_action(ACT_START2, down); hit = 1; }
#ifdef HOTD4_DESKTOP_PAUSE
    }
#endif
    return hit;
}

/* ---- remapping and calibration, started from the menus ---- */
void host_begin_capture(HostAction a)
{
    g_calib = 0;
    g_capture = (int)a;
    char t[160];
    snprintf(t, sizeof t, "Press a key or mouse button for \"%s\" (Esc cancels)", settings_action_name(a));
    set_title(t);
}

static void finish_capture(int bind)
{
    HostSettings *st = settings();
    if (bind) {
        for (int a = 0; a < ACT_COUNT; a++)       /* one action per input */
            if (st->bind[a] == bind) st->bind[a] = 0;
        st->bind[g_capture] = bind;
        settings_save();
        char nm[64]; settings_binding_name(bind, nm, sizeof nm);
        fprintf(stderr, "[settings] %s -> %s\n", settings_action_name((HostAction)g_capture), nm);
    }
    g_capture = -1;
    g_held = 0;
    set_title(NULL);
}

/* Aim calibration: the gun is held at two known points in turn and the player
 * clicks where the game draws its sight; the two pairs give scale and offset
 * per axis. Points are in window pixels, before the correction. */
#define CAL_AX 0.30
#define CAL_BX 0.70
#define CAL_AY 0.30
#define CAL_BY 0.70

void host_begin_aim_calibration(void)
{
    g_capture = -1;
    g_calib = 1;
    set_title("Aim calibration 1/2: click the centre of the game's sight (Esc cancels)");
}

static void calib_click(int x, int y)
{
    double w = g_win.w ? g_win.w : DEFAULT_W, h = g_win.h ? g_win.h : DEFAULT_H;
    if (g_calib == 1) {
        g_calib_sx = x; g_calib_sy = y; g_calib = 2;
        set_title("Aim calibration 2/2: click the centre of the game's sight again (Esc cancels)");
        return;
    }
    HostSettings *st = settings();
    double ax = CAL_AX * w, bx = CAL_BX * w, ay = CAL_AY * h, by = CAL_BY * h;
    double sx = (x - g_calib_sx) / (bx - ax), sy = (y - g_calib_sy) / (by - ay);
    if (sx > 0.5 && sx < 2.0 && sy > 0.5 && sy < 2.0) {
        st->aim_sx = sx; st->aim_ox = g_calib_sx - sx * ax;
        st->aim_sy = sy; st->aim_oy = g_calib_sy - sy * ay;
        settings_save();
        fprintf(stderr, "[settings] aim calibrated: %.3fx%+.1f, %.3fy%+.1f\n", st->aim_sx, st->aim_ox, st->aim_sy, st->aim_oy);
    } else {
        fprintf(stderr, "[settings] aim calibration rejected (scale %.2f, %.2f) - kept the old one\n", sx, sy);
    }
    g_calib = 0;
    set_title(NULL);
}

/* host_gun_map: where the sight should be (game pixels) -> the reading the
 * game must get for it there, for a gun the game does not read as a plain
 * screen position. */
static void (*g_gun_map)(int *x, int *y);
void host_gun_map(void (*fn)(int *x, int *y)) { g_gun_map = fn; }

/* LINDBERGH_AUTOSTART=1: a coin at frame 240, then start pressed every two
 * seconds - gets past attract mode with nobody at the keyboard. */
static void cabinet_autostart(void)
{
    static int on = -1, coined;
    if (on < 0) on = env_int("LINDBERGH_AUTOSTART", 0) != 0;
    if (!on) return;
    /* LINDBERGH_AUTOSTART=N with N > 1 inserts N coins, a second apart:
     * The House of the Dead 4 takes two to start. */
    static int want = -1;
    if (want < 0) want = env_int("LINDBERGH_AUTOSTART", 1);
    static unsigned last_coin_frame;
    if (!coined && g_frame > 240 && g_frame % 60u == 0 && g_frame != last_coin_frame) {
        last_coin_frame = g_frame;
        g_cab.coins[0]++;
        fprintf(stderr, "[cab] autostart: coin inserted at frame %u\n", g_frame);
        static int n;
        if (++n >= want) coined = 1;
    }
    if (coined && !(getenv("HOTD4SP_CHAPTER") && SDL_AtomicGet(&sp_practice_ready))) {
        if (g_frame % 120u < 20u) g_auto |= CAB_P1_START;
        else                      g_auto &= ~CAB_P1_START;
    } else if(getenv("HOTD4SP_CHAPTER") && SDL_AtomicGet(&sp_practice_ready))g_auto &= ~CAB_P1_START;
    /* LINDBERGH_AUTOGUN=1 as well: the gun circles the screen and fires in
     * half-second bursts, to check aim and trigger without a mouse */
    static int gun = -1;
    if (gun < 0) gun = env_int("LINDBERGH_AUTOGUN", 0);
    if (gun && coined) {
        g_scripted = 1;
        double t = g_frame / 90.0;
        int w = g_win.w ? g_win.w : DEFAULT_W, h = g_win.h ? g_win.h : DEFAULT_H;
        int ax, ay;     /* LINDBERGH_AIM=x,y holds the gun still instead */
        const char *aim = getenv("LINDBERGH_AIM");
        /* LINDBERGH_AIM_BOX=<n>: at the centre of route box n while one is up;
         * =<x>,<y>: at that point while any is */
        const char *box = getenv("LINDBERGH_AIM_BOX");
        float r[4];
        int bx, by;
        if (box && strchr(box, ',') && sscanf(box, "%d,%d", &bx, &by) == 2 && stereo_aim_box_rect(0, r)) { g_mouse_x = bx; g_mouse_y = by; }
        else if (box && !strchr(box, ',') && stereo_aim_box_rect(atoi(box), r)) { g_mouse_x = (int)((r[0] + r[2]) / 2); g_mouse_y = (int)((r[1] + r[3]) / 2); }
        else if (aim && sscanf(aim, "%d,%d", &ax, &ay) == 2) { g_mouse_x = ax; g_mouse_y = ay; }
        else {
            g_mouse_x = (int)(w / 2 + w / 3 * cos(t));
            g_mouse_y = (int)(h / 2 + h / 3 * sin(t));
        }
        g_auto &= ~(CAB_P1_TRIGGER | CAB_P1_RELOAD);
        if (g_frame % 60u < 30u) g_auto |= CAB_P1_TRIGGER;
        /* LINDBERGH_AUTOPRESS=<cab bits, hex>: also press those for a quarter
         * second every two seconds - for finding what a button does */
        static int press = -1;
        if (press < 0) { const char *v = getenv("LINDBERGH_AUTOPRESS"); press = v ? (int)strtol(v, NULL, 16) : 0; }
        g_auto &= ~(unsigned)press;
        /* Pedal minigames need repeat presses faster than the default
         * diagnostic pulse. These affect AUTOPRESS only, never real input. */
        static unsigned press_period, press_hold;
        if (!press_period) {
            int period = env_int("LINDBERGH_AUTOPRESS_PERIOD", 120);
            int hold = env_int("LINDBERGH_AUTOPRESS_HOLD", 15);
            press_period = period > 1 ? (unsigned)period : 120;
            press_hold = hold > 0 && (unsigned)hold < press_period / 2 ? (unsigned)hold : press_period / 2;
        }
        unsigned phase = g_frame % press_period;
        if (press && phase >= press_period / 2 && phase < press_period / 2 + press_hold) g_auto |= (unsigned)press;
        /* LINDBERGH_AUTOGUN=2: one second in four off the screen, with a
         * press of the gun's screen-out signal - the arcade's reload */
        if (gun == 2) stereo_game_gun_reloading(g_frame % 240u >= 20u && g_frame % 240u < 110u);
        if (gun == 2 && g_frame % 240u < 60u) {
            g_mouse_x = -100000; g_mouse_y = -100000;
            g_auto &= ~CAB_P1_TRIGGER;
            if (g_frame % 240u >= 20u && g_frame % 240u < 30u) g_auto |= CAB_P1_RELOAD;
        }
    }
}

/* LINDBERGH_SCRIPT=<file>: scripted play, for reaching a scene unattended.
 * Each line "<from> <to> <x> <y> <buttons, hex> [coins]": from game frame
 * <from> up to <to>, the gun at x,y on the 640x480 screen (negative: off it)
 * with those cabinet buttons held; coins go in once, at <from>. */
static void cabinet_script(void)
{
    static int loaded;
    static struct { unsigned from, to; int x, y; unsigned buttons, coins; } ln[256];
    static int n;
    if (!loaded) {
        loaded = 1;
        const char *path = getenv("LINDBERGH_SCRIPT");
        FILE *f = path && *path ? fopen(path, "r") : NULL;
        char buf[160];
        while (f && n < 256 && fgets(buf, sizeof buf, f)) {
            unsigned c = 0;
            if (buf[0] == '#') continue;
            if (sscanf(buf, "%u %u %d %d %x %u", &ln[n].from, &ln[n].to, &ln[n].x, &ln[n].y, &ln[n].buttons, &c) >= 5) {
                ln[n].coins = c;
                n++;
            }
        }
        if (f) { fclose(f); fprintf(stderr, "[cab] script: %d lines from %s\n", n, path); }
        else if (path && *path) fprintf(stderr, "[cab] script %s not found\n", path);
    }
    if (!n) return;
    int any = 0;
    for (int i = 0; i < n; i++) any |= g_frame >= ln[i].from && g_frame <= ln[i].to;
    static int was;
    if (!any) {                 /* between the script's lines, the real gun (or LINDBERGH_AUTOGUN's) */
        if (was) { g_scripted = 0; g_auto = 0; }
        was = 0;
        return;
    }
    was = 1;
    g_scripted = 1;
    g_auto = 0;
    int w = g_win.w ? g_win.w : DEFAULT_W, h = g_win.h ? g_win.h : DEFAULT_H;
    g_mouse_x = -100000; g_mouse_y = -100000;
    for (int i = 0; i < n; i++) {
        if (g_frame < ln[i].from || g_frame > ln[i].to) continue;
        if (ln[i].x == -2) {    /* x -2: sweep the screen, as LINDBERGH_AUTOGUN does */
            double t = g_frame / 90.0;
            g_mouse_x = (int)(w / 2 + w / 3 * cos(t)); g_mouse_y = (int)(h / 2 + h / 4 * sin(t * 1.7));
        } else if (ln[i].x >= 0 && ln[i].y >= 0) { g_mouse_x = ln[i].x * w / 640; g_mouse_y = ln[i].y * h / 480; }
        g_auto |= ln[i].buttons;
        if (g_frame == ln[i].from) {
            g_cab.coins[0] += ln[i].coins;
            ln[i].coins = 0; /* host_pump can run several times in one frame */
        }
    }
}

/* Where a pointer at px,py puts the cabinet's gun: the game's own calibration
 * undone, so the sight is under the pointer, and a game whose gun is not a
 * plain screen position mapped (on the screen only: off it is the reload). */
static void gun_reading(int px, int py, double w, double h, int *gx, int *gy)
{
    HostSettings *st = settings();
    *gx = (int)((px - st->aim_ox) / st->aim_sx);
    *gy = (int)((py - st->aim_oy) / st->aim_sy);
    if (g_gun_map && *gx >= 0 && *gy >= 0 && *gx < w && *gy < h) g_gun_map(gx, gy);
}

/* LINDBERGH_P2_TEST=1: a second, scripted gun - a coin and P2 Start every
 * eight seconds from frame 600, its own sweep (the other way round to P1's
 * auto-gun) and half-second bursts, one second in six off the screen with
 * its reload - for checking a game takes Player 2's gun. */
static void cabinet_p2_test(void)
{
    static int on = -1;
    if (on < 0) on = env_int("LINDBERGH_P2_TEST", 0);
    if (!on || g_frame < 600) return;
    g_gun2_on = 1;
    int w = g_win.w ? g_win.w : DEFAULT_W, h = g_win.h ? g_win.h : DEFAULT_H;
    double t = g_frame / 70.0;
    g_gun2_px = (int)(w / 2 - w / 3 * cos(t));
    g_gun2_py = (int)(h / 2 + h / 4 * sin(t * 1.3));
    unsigned p2 = CAB_P2_TRIGGER | CAB_P2_RELOAD | CAB_P2_START;
    g_auto &= ~p2;
    static unsigned coined;
    unsigned coin_at = on == 3 ? 30u : 0u;   /* =3: the coin in the frame Start goes down */
    if (g_frame % 480u == coin_at && coined != g_frame) { coined = g_frame; g_cab.coins[0]++; fprintf(stderr, "[cab] p2 test: coin at frame %u\n", g_frame); }
    if (on != 2 && g_frame % 480u >= 30u && g_frame % 480u < 50u) g_auto |= CAB_P2_START;   /* =2: never joins */
    if (g_frame % 360u < 60u) {
        g_gun2_px = g_gun2_py = -100000;
        if (g_frame % 360u >= 20u && g_frame % 360u < 30u) g_auto |= CAB_P2_RELOAD;
    } else if (g_frame % 40u < 20u) g_auto |= CAB_P2_TRIGGER;
}

void host_cabinet_input(CabinetInput *out)
{
    cabinet_autostart();
    cabinet_script();
    double w = g_win.w ? g_win.w : DEFAULT_W, h = g_win.h ? g_win.h : DEFAULT_H;
    if (g_calib == 1)      { g_cab.gun_x = (int)(CAL_AX * w); g_cab.gun_y = (int)(CAL_AY * h); }
    else if (g_calib == 2) { g_cab.gun_x = (int)(CAL_BX * w); g_cab.gun_y = (int)(CAL_BY * h); }
    else gun_reading(g_mouse_x, g_mouse_y, w, h, &g_cab.gun_x, &g_cab.gun_y);
    cabinet_p2_test();
    g_cab.gun2 = g_gun2_on;
    if (g_gun2_on) gun_reading(g_gun2_px, g_gun2_py, w, h, &g_cab.gun2_x, &g_cab.gun2_y);
#if !defined(LINDBERGH_OPENXR)
    if (stereo_wide(NULL, NULL, NULL) || stereo_enabled()) {      /* the HUD keeps clear of them */
        stereo_set_gun(g_cab.gun_x, g_cab.gun_y);
        stereo_set_gun2(g_gun2_on ? g_cab.gun2_x : -100000, g_gun2_on ? g_cab.gun2_y : -100000);
    }
#endif
    g_cab.screen_w = (int)w;
    g_cab.screen_h = (int)h;
    g_cab.buttons = g_auto | (g_scripted ? 0 : g_held | g_pad_held | raw_held | remote_held);
    *out = g_cab;
    if (!g_scripted) out->buttons |= g_cab_latch;
    if(!game_profile()->joystick)out->buttons=gameplay_offscreen_buttons(out->buttons,gameplay_offscreen_reload(),g_mouse_x,g_mouse_y,g_gun2_on,g_gun2_px,g_gun2_py,g_win.w,g_win.h);
    if (g_cab_button_filter) out->buttons = g_cab_button_filter(out->buttons);
    {   /* What the board reports, whenever it changes - and the gun, at most
         * once a second - so a report of "nothing registers" can be checked
         * against what the game was actually given. */
        static unsigned lb = ~0u, lc0 = ~0u, n; static int lx = -1, ly = -1;
        if (out->buttons != lb || out->coins[0] != lc0) {
            fprintf(stderr, "[cab] frame %u: buttons %#x, coins %u\n", g_frame, out->buttons, out->coins[0]);
            lb = out->buttons; lc0 = out->coins[0];
        }
        if (++n % 60 == 0 && (out->gun_x != lx || out->gun_y != ly)) {
            fprintf(stderr, "[cab] frame %u: gun %d,%d (pointer %d,%d)\n", g_frame, out->gun_x, out->gun_y, g_mouse_x, g_mouse_y);
            lx = out->gun_x; ly = out->gun_y;
        }
    }
}

/* The buttons the board would report now, without the bookkeeping of a read:
 * for a game hook that needs one switch (a gun's reload) between reads. */
unsigned host_cabinet_buttons(void)
{
    unsigned buttons = g_auto | (g_scripted ? 0 : g_held | g_pad_held | raw_held | remote_held | g_cab_latch);
    if(!game_profile()->joystick)buttons=gameplay_offscreen_buttons(buttons,gameplay_offscreen_reload(),g_mouse_x,g_mouse_y,g_gun2_on,g_gun2_px,g_gun2_py,g_win.w,g_win.h);
    return g_cab_button_filter ? g_cab_button_filter(buttons) : buttons;
}

/* A headset's controllers (host_xr.c): where the gun points, in the game's
 * pixels, and the cabinet buttons held. */
void host_vr_input(int gun_x, int gun_y, unsigned held, int coin)
{
    g_mouse_x = gun_x; g_mouse_y = gun_y;
    g_cab_latch |= held & ~g_held;
    g_held = held;
    if (coin) g_cab.coins[0] += (unsigned)coin;
}

/* Two guns (dual wielding): the second one's place too; on 0, P1's alone. */
void host_vr_input2(int on, int gun2_x, int gun2_y)
{
    g_gun2_on = on;
    g_gun2_px = gun2_x; g_gun2_py = gun2_y;
}

/* The board has reported the switches: latched taps have been seen. */
void host_cabinet_switches_read(void)
{
    static int lat = -1; static Uint64 last; static unsigned reads;
    if (lat < 0) lat = env_int("LINDBERGH_LATENCY", 0);
    if (lat) {
        Uint64 now = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency();
        double gap = last ? (now - last) * 1000.0 / hz : 0; last = now; reads++;
        if (gap > 40) fprintf(stderr, "[lat] frame %u: switch reads %.0f ms apart\n", g_frame, gap);
        if (g_lat_t) {
            fprintf(stderr, "[lat] trigger: board reported it %.1f ms / %u frames after the click\n",
                    (now - g_lat_t) * 1000.0 / hz, g_frame - g_lat_f);
            g_lat_t = 0;
        }
    }
    g_cab_latch = 0;
}

/* SDL keysym -> Win32 virtual key: letters upper case, digits as themselves. */
static int vk_of(SDL_Keycode k)
{
    if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
    if (k >= ' ' && k < 0x7F) return k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return VK_F1 + (k - SDLK_F1);
    switch (k) {
    case SDLK_LEFT:  return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_UP:    return VK_UP;
    case SDLK_DOWN:  return VK_DOWN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_RETURN: return '\r';
    }
    return 0;
}

static int button_of(Uint8 b)
{
    return b == SDL_BUTTON_LEFT ? 0 : b == SDL_BUTTON_MIDDLE ? 1 : 2;
}

/* LINDBERGH_KEYS="frame:key,frame:key...": push real SDL key events - a
 * press at that frame, the release in the same drain (a fast tap) - so the
 * keyboard path is tested exactly as a person drives it. "!" is the pointer
 * leaving the window and coming back. */
static void fake_keys(void)
{
    static const char *v = (const char *)1;
    static unsigned done = ~0u;              /* events drain more than once a frame */
    if (v == (const char *)1) v = getenv("LINDBERGH_KEYS");
    if (!v || done == g_frame) return;
    done = g_frame;
    for (const char *p = v; p && *p; ) {
        unsigned f = (unsigned)strtoul(p, (char **)&p, 10);
        if (*p++ != ':' || !*p) break;
        char k = *p++;
        if (f == g_frame) {
            SDL_Event e; memset(&e, 0, sizeof e);
            if (k == '!') {                  /* the pointer leaves the window */
                e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_LEAVE;
                SDL_PushEvent(&e);
                e.window.event = SDL_WINDOWEVENT_ENTER; SDL_PushEvent(&e);
                fprintf(stderr, "[keys] pointer out and back at frame %u\n", f);
                while (*p == ',') p++;
                continue;
            }
            if (k == '@') {                  /* a left click where the pointer is */
                e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
                e.button.x = g_mouse_x; e.button.y = g_mouse_y;
                to_window(&e.button.x, &e.button.y);
                SDL_PushEvent(&e);
                e.type = SDL_MOUSEBUTTONUP; SDL_PushEvent(&e);
                fprintf(stderr, "[keys] clicked at frame %u\n", f);
                while (*p == ',') p++;
                continue;
            }
            e.type = SDL_KEYDOWN; e.key.keysym.sym = (SDL_Keycode)(k >= 'A' && k <= 'Z' ? k + 32 : k);
            SDL_PushEvent(&e);
            e.type = SDL_KEYUP; SDL_PushEvent(&e);
            fprintf(stderr, "[keys] tapped %c at frame %u\n", k, f);
        }
        while (*p == ',') p++;
    }
}

static int g_capture_or_calib(void) { return g_capture >= 0 || g_calib; }

static char g_title[128] = "lindberghrecomp";
static void set_title(const char *msg)
{
    if (g_win.win) SDL_SetWindowTitle(g_win.win, msg ? msg : g_title);
}

#ifdef HOTD4_DESKTOP_PAUSE
static void raw_mouse_publish(void)
{
    if (g_scripted || !settings()->raw_mouse || !g_relative_mode || !SDL_GetRelativeMouseMode()) {
        g_raw_update_tick = 0;
        return;
    }
    Uint64 now = input_mouse_smoothing ? SDL_GetPerformanceCounter() : 0;
    if (!now || !g_raw_update_tick) {
        g_raw_filtered_x = g_raw_x;
        g_raw_filtered_y = g_raw_y;
    } else if (g_raw_filtered_x != g_raw_x || g_raw_filtered_y != g_raw_y) {
        float dt = (float)((double)(now - g_raw_update_tick) / SDL_GetPerformanceFrequency());
        float time_constant = input_mouse_smoothing == 1 ? 0.008f : 0.016f;
        float weight = 1.0f - expf(-dt / time_constant);
        g_raw_filtered_x += (g_raw_x - g_raw_filtered_x) * weight;
        g_raw_filtered_y += (g_raw_y - g_raw_filtered_y) * weight;
        if (fabsf(g_raw_x - g_raw_filtered_x) < 0.25f) g_raw_filtered_x = g_raw_x;
        if (fabsf(g_raw_y - g_raw_filtered_y) < 0.25f) g_raw_filtered_y = g_raw_y;
    }
    g_raw_update_tick = now;
    int x = (int)lroundf(g_raw_filtered_x), y = (int)lroundf(g_raw_filtered_y);
    if (x != g_mouse_x || y != g_mouse_y) {
        g_mouse_x = x;
        g_mouse_y = y;
        in_push(HOST_IN_MOTION, x, y, g_buttons, 0);
    }
}
#endif

static void pump(void)
{
    SDL_Event e;
    fake_keys();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            pad_event(&e);
            break;
        case SDL_QUIT:
            fprintf(stderr, "[window] closed by the user\n");
            host_quit();
        case SDL_WINDOWEVENT:
#if defined(__APPLE__)
            /* SDL refreshes the GL context on a resize, which can drop the
             * fixed drawable size: pin it again */
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && g_win.gl) {
                fprintf(stderr, "[window] resized to %dx%d\n", e.window.data1, e.window.data2);
                { int pw, ph; picture_size(&pw, &ph); host_mac_fix_surface(g_win.win, pw, ph); }
            }
#endif
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                fprintf(stderr, "[window] focus gained\n");
                if (settings()->raw_mouse && g_relative_mode) {
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                } else if (settings()->mouse_grab) {
                    SDL_SetWindowGrab(g_win.win, SDL_TRUE);
                }
            }
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                host_mouse_shake(0);
                fprintf(stderr, "[window] focus lost\n");
                if (settings()->raw_mouse && g_relative_mode) {
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                }
            }
            if (e.window.event == SDL_WINDOWEVENT_ENTER) { fprintf(stderr, "[window] pointer entered\n"); g_pointer_in = 1; }
            if (e.window.event == SDL_WINDOWEVENT_LEAVE) {
                fprintf(stderr, "[window] pointer left\n");
                /* Pointing a real gun off the screen is the reload; the
                 * nearest thing a mouse can do is leave the window. */
                if (input_active_mode < 2 && !g_relative_mode && g_pointer_in && settings()->leave_reload && !game_profile()->joystick && !g_capture_or_calib()) g_cab_latch |= CAB_P1_RELOAD;
                g_pointer_in = 0;
            }
            break;
        case SDL_MOUSEMOTION:
            if (input_active_mode >= 2) break;
            pthread_mutex_lock(&g_mouse_shake_mutex);
            if (g_mouse_shake_active)
                mouse_shake_motion(&g_mouse_shake, e.motion.xrel, e.motion.yrel, SDL_GetTicks());
            pthread_mutex_unlock(&g_mouse_shake_mutex);
            if (settings()->raw_mouse && g_relative_mode) {
                float sens = settings()->mouse_sens > 0.0f ? settings()->mouse_sens : 1.0f;
#ifdef HOTD4_DESKTOP_PAUSE
                sens = input_mouse_speed;
#endif
                if (g_raw_x < 0) {
                    g_raw_x = g_win.w / 2;
                    g_raw_y = g_win.h / 2;
                }
                g_raw_x += e.motion.xrel * sens;
                g_raw_y += e.motion.yrel * sens;
                if (g_raw_x < 0) g_raw_x = 0;
                if (g_raw_x > g_win.w - 1) g_raw_x = g_win.w - 1;
                if (g_raw_y < 0) g_raw_y = 0;
                if (g_raw_y > g_win.h - 1) g_raw_y = g_win.h - 1;
#ifdef HOTD4_DESKTOP_PAUSE
                if (!input_mouse_smoothing) raw_mouse_publish();
                break;
#endif
                g_mouse_x = g_raw_x;
                g_mouse_y = g_raw_y;
            } else {
                g_mouse_x = e.motion.x;
                g_mouse_y = e.motion.y;
                to_game(&g_mouse_x, &g_mouse_y);
            }
            in_push(HOST_IN_MOTION, g_mouse_x, g_mouse_y, g_buttons, 0);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (input_active_mode >= 2) break;
            int b = button_of(e.button.button), down = e.type == SDL_MOUSEBUTTONDOWN;
            if (settings()->raw_mouse && g_relative_mode) {
                /* g_mouse_x and g_mouse_y are already maintained in game coords */
            } else {
                if (down && settings()->raw_mouse && !g_relative_mode) {
                    g_relative_mode = 1;
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                }
                g_mouse_x = e.button.x;
                g_mouse_y = e.button.y;
                to_game(&g_mouse_x, &g_mouse_y);
            }
            e.button.x = g_mouse_x; e.button.y = g_mouse_y;
            if (down && g_capture >= 0) { finish_capture(MOUSE_BIND(e.button.button)); break; }
            if (down && g_calib) { calib_click(e.button.x, e.button.y); break; }
            if (down) g_buttons |= 1 << b; else g_buttons &= ~(1 << b);
            cabinet_input(MOUSE_BIND(e.button.button), down);
            in_push(HOST_IN_MOUSE, b, down ? 0 : 1, g_mouse_x, g_mouse_y);
            break;
        }
        case SDL_KEYDOWN: {
            if (e.key.repeat) break;
            if ((e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT) && !hc_custom) || hc_matches(HC_FULLSCREEN,e.key.keysym.sym)) {
                Uint32 fl = SDL_GetWindowFlags(g_win.win);
                int is_fs = (fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
                SDL_SetWindowFullscreen(g_win.win, is_fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                fprintf(stderr, "[window] Fullscreen toggled %s\n", !is_fs ? "ON" : "OFF");
                break;
            }
            if (hc_matches(HC_MOUSELOCK,e.key.keysym.sym)) {
                if (input_active_mode >= 2) break;
                if (settings()->raw_mouse) {
                    g_relative_mode = !g_relative_mode;
                    SDL_SetRelativeMouseMode(g_relative_mode ? SDL_TRUE : SDL_FALSE);
                    fprintf(stderr, "[input] Raw mouse %s (Mouse Lock shortcut)\n", g_relative_mode ? "locked" : "unlocked");
                } else {
                    SDL_bool gr = SDL_GetWindowGrab(g_win.win);
                    SDL_SetWindowGrab(g_win.win, gr ? SDL_FALSE : SDL_TRUE);
                    fprintf(stderr, "[input] Window grab %s (Mouse Lock shortcut)\n", !gr ? "locked" : "unlocked");
                }
                break;
            }
            if (hc_matches(HC_CROSSHAIR,e.key.keysym.sym)) {
                settings()->crosshair = !settings()->crosshair;
                fprintf(stderr, "[crosshair] Crosshair %s (Toggle Crosshair shortcut)\n", settings()->crosshair ? "ON" : "OFF");
                break;
            }
            if(hc_matches(HC_RESTART,e.key.keysym.sym)){fprintf(stderr,"[controls] restart shortcut\n");exit(74);}
            int vk = vk_of(e.key.keysym.sym);
            fprintf(stderr, "[key] down %s (vk %#x)\n", SDL_GetKeyName(e.key.keysym.sym), vk);
            if (g_capture >= 0 || g_calib) {
                if (e.key.keysym.sym == SDLK_ESCAPE) { g_capture = -1; g_calib = 0; set_title(NULL); }
                else if (g_capture >= 0) finish_capture((int)e.key.keysym.sym);
                break;
            }
            if (hc_matches(HC_PAUSE,e.key.keysym.sym)) {
#ifdef HOTD4_DESKTOP_PAUSE
                if (hotd4_pause_enabled()) {
                    g_pause_requested = 1;
                    return;
                }
#endif
                fprintf(stderr, "[window] Escape pressed - quitting\n");
                exit(0);
            }
            cabinet_input((int)e.key.keysym.sym, 1);
            if (!vk) break;
            if (e.key.keysym.sym < 0x80)
                in_push(HOST_IN_KEY, (int)e.key.keysym.sym, 1, g_mouse_x, g_mouse_y);
            else
                in_push(HOST_IN_SPECIAL, vk, 1, g_mouse_x, g_mouse_y);
            break;
        }
        case SDL_KEYUP: {
            int vk = vk_of(e.key.keysym.sym);
            cabinet_input((int)e.key.keysym.sym, 0);
            if (!vk) break;
            in_push(HOST_IN_KEYUP, vk, 0, g_mouse_x, g_mouse_y);
            break;
        }
        }
    }
#ifdef HOTD4_DESKTOP_PAUSE
    raw_mouse_publish();
    remote_mouse_drain();
    remote_mouse_publish();
#endif
    pad_poll();
}


void host_window_hint(int w, int h)
{
    if (w > 0 && h > 0 && !g_win.win) { g_hint_w = w; g_hint_h = h; }
}

/* The window's title before the game names it (a GLX game may never). */
void host_window_title(const char *title)
{
    if (title && *title) snprintf(g_title, sizeof g_title, "%s", title);
#if defined(__APPLE__)
    { extern void host_mac_app_name(const char *); host_mac_app_name(title); }
#endif
}

int host_window_open(const char *title)
{
#ifdef HOTD4_DESKTOP_PAUSE
    input_options_load();
#endif
    if (title && *title) snprintf(g_title, sizeof g_title, "%s", title);
    if (g_win.win) {
        if (title && *title) SDL_SetWindowTitle(g_win.win, title);
        return 1;
    }
#ifdef LINDBERGH_VULKAN_OFFSCREEN
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
#endif
#if defined(__ANDROID__)
    /* SDL selects its Android event pump when the video device is created.
     * Setting this after SDL_Init leaves the blocking pump installed, which
     * can stop the game/render owner from polling OpenXR during an Activity
     * pause. OpenXR session state remains responsible for XR frame delivery. */
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
#endif
#ifdef _WIN32
    typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(HANDLE);
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (u32) {
        SetProcessDpiAwarenessContextProc set_dpi = (SetProcessDpiAwarenessContextProc)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (set_dpi) {
            set_dpi((HANDLE)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
        }
    }
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
#endif
#if defined(LINDBERGH_OPENXR)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
#else
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
#endif
        fprintf(stderr, "[window] SDL_Init: %s\n", SDL_GetError());
        return 0;
    }

    g_win.w = env_int("LINDBERGH_WIDTH", g_hint_w ? g_hint_w : DEFAULT_W);
    g_win.h = env_int("LINDBERGH_HEIGHT", g_hint_h ? g_hint_h : DEFAULT_H);

#ifndef LINDBERGH_VULKAN
#if defined(__ANDROID__)
    /* GLES 3 under gl4es, which plays the 2.1 context the game expects. The
     * headset keeps running while the activity is paused. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(LINDBERGH_OPENXR) && defined(_WIN32)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#else
    /* The game is a 2007 fixed-function + ARB-program GL program: a 2.1
     * compatibility context is what it was written for. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    if (settings()->aa > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, settings()->aa);
    }
#endif
    /* The window: LINDBERGH_WINDOW=WxH, settings(), or picture_size().
     * Scaled on present_scale() to keep aspect ratio. */
    int dw = 0, dh = 0;
    {
        const char *v = getenv("LINDBERGH_WINDOW");
        if (v && sscanf(v, "%dx%d", &dw, &dh) == 2 && dw > 0 && dh > 0) {
            /* env override */
        } else if (settings()->window_w > 0 && settings()->window_h > 0) {
            dw = settings()->window_w;
            dh = settings()->window_h;
        } else {
            picture_size(&dw, &dh);
        }
    }
    /* LINDBERGH_HIDDEN=1: a test run that never shows its window or takes
     * focus; LINDBERGH_SHOT still captures the frames */
    const char *hv = getenv("LINDBERGH_HIDDEN");
    int hidden = hv && *hv && *hv != '0';
    Uint32 win_flags = 
#if defined(LINDBERGH_VULKAN_OFFSCREEN)
        0 |
#elif defined(LINDBERGH_VULKAN_XR) && defined(__ANDROID__)
        SDL_WINDOW_FULLSCREEN |
#elif defined(LINDBERGH_VULKAN_XR)
        /* desktop OpenXR (Linux, Steam Frame): the
         * headset is the display; the window only
         * carries SDL's events */
        SDL_WINDOW_HIDDEN |
#elif defined(LINDBERGH_VULKAN)
        SDL_WINDOW_VULKAN |
#else
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
#endif
        (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    if (settings()->fullscreen && !hidden) {
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    g_win.win = SDL_CreateWindow(title && *title ? title : "lindberghrecomp",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 dw, dh, win_flags);
    if (!g_win.win && settings()->aa > 1) {
        fprintf(stderr,"[window] MSAA %dx window unavailable, retrying without MSAA\n",settings()->aa);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
        g_win.win=SDL_CreateWindow(title && *title ? title : "lindberghrecomp",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,dw,dh,win_flags);
    }
    if (!g_win.win) {
        fprintf(stderr, "[window] SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }
#if defined(__APPLE__)
    if (!hidden) {
        mac_activate();
        host_menu_install();
    }
#endif
#ifdef HOTD4_DESKTOP_PAUSE
    if (!raw_install()) {
        fprintf(stderr,"[raw-gun] Registration failed: Windows error %lu\n",GetLastError());
        MessageBoxA(NULL,"Windows Raw Input could not be initialized. Select a mouse mode in Settings.","HOTD4 Input",MB_OK|MB_ICONERROR);
        return 0;
    }
#endif
    hc_init();
    if (!hidden) SDL_RaiseWindow(g_win.win);
    /* The desktop game draws its own configurable sight. Keep the OS cursor
     * hidden during gameplay; the pause loop temporarily shows a normal arrow. */
    {
#ifdef HOTD4_DESKTOP_PAUSE
        SDL_Cursor *cur = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
#else
        SDL_Cursor *cur = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
#endif
        if (cur) SDL_SetCursor(cur);
    }
    SDL_SetHint("SDL_MOUSE_RELATIVE_RAW_INPUT", "1");
    SDL_SetHint("SDL_MOUSE_RELATIVE_MODE_WARP", "0");
    if (settings()->raw_mouse) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        g_relative_mode = 1;
        g_raw_x = g_win.w / 2;
        g_raw_y = g_win.h / 2;
        g_mouse_x = g_raw_x;
        g_mouse_y = g_raw_y;
        fprintf(stderr, "[input] Raw mouse mode active (sens: %.2f, F1 toggles lock)\n", (double)settings()->mouse_sens);
    } else if (settings()->mouse_grab) {
        SDL_SetWindowGrab(g_win.win, SDL_TRUE);
        fprintf(stderr, "[input] Mouse grab active (Mouse Lock toggles grab)\n");
    }
#ifdef HOTD4_DESKTOP_PAUSE
    SDL_ShowCursor(SDL_DISABLE);
#endif
    fprintf(stderr, "[window] game %dx%d, window %dx%d%s (fullscreen: %d, aa: %d, af: %d, raw_mouse: %d)\n",
            g_win.w, g_win.h, dw, dh,
#if defined(__APPLE__)
            pthread_main_np() ? "" : " (NOT on the main thread)",
#else
            "",
#endif
            settings()->fullscreen, settings()->aa, settings()->af, settings()->raw_mouse
            );
    // Register exit output before worker shutdown, so LIFO teardown drains
    // all renderer records before writing the capture. No per-frame file I/O.
#if defined(LINDBERGH_VULKAN_OFFSCREEN)
    frame_timing_init("vulkan-offscreen-diagnostic");
#elif defined(LINDBERGH_VULKAN_XR)
    frame_timing_init("vulkan-openxr");
#elif defined(LINDBERGH_VULKAN)
    frame_timing_init("vulkan");
#elif defined(LINDBERGH_OPENXR)
    frame_timing_init("opengl-openxr");
#else
    frame_timing_init("opengl");
#endif
    return 1;
}

int host_gl_context(void)
{
    if (!g_win.win && !host_window_open(NULL)) return 0;
#ifdef LINDBERGH_VULKAN
    return lvk_host_init(g_win.win, g_win.w, g_win.h);
#else
    if (g_win.gl) return 1;
    g_win.gl = SDL_GL_CreateContext(g_win.win);
    if (!g_win.gl && settings()->aa > 1) {
        fprintf(stderr, "[window] MSAA %dx failed, retrying without MSAA\n", settings()->aa);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        g_win.gl = SDL_GL_CreateContext(g_win.win);
    }
    if (!g_win.gl) {
        fprintf(stderr, "[window] SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 0;
    }
#if defined(LINDBERGH_OPENXR)
#ifdef _WIN32
    GLenum glew_status = glewInit();
    if (glew_status != GLEW_OK) {
        fprintf(stderr, "[window] GLEW: %s\n", glewGetErrorString(glew_status));
        return 0;
    }
#endif
#ifdef LINDBERGH_GL4ES
    set_getprocaddress(SDL_GL_GetProcAddress);
    set_getmainfbsize(main_fb_size);
    initialize_gl4es();
#endif
    if (!xr_init()) {
        fprintf(stderr, "[window] OpenXR startup failed. Check the active runtime and headset connection.\n");
#ifdef _WIN32
        /* The arcade game continues with invalid GL state after a failed context request. */
        exit(EXIT_FAILURE);
#else
        return 0;
#endif
    }
    xr_frame_begin();
#else
#ifdef _WIN32
    if (glewInit() != GLEW_OK) return 0;
#endif
    {
        GLint samples = 0; glGetIntegerv(GL_SAMPLES, &samples);
        if(settings()->aa > 1 && samples > 1) glEnable(GL_MULTISAMPLE);
        else glDisable(GL_MULTISAMPLE);
        fprintf(stderr,"[window] MSAA requested: %d; actual framebuffer: %d samples\n",settings()->aa,samples);
    }
    {
        int vx = 0, vy = 0, vw = g_win.w, vh = g_win.h;
        host_viewport_scale(&vx, &vy, &vw, &vh);
        glViewport(vx, vy, vw, vh);
        glScissor(vx, vy, vw, vh);
    }
#if defined(__APPLE__)
    { int pw, ph; picture_size(&pw, &ph); host_mac_fix_surface(g_win.win, pw, ph); }
    /* the context started with the window's size as its viewport and
     * scissor box, which the game reads back: make them its own */
    glViewport(0, 0, g_win.w, g_win.h);
    glScissor(0, 0, g_win.w, g_win.h);
#endif
    SDL_GL_SetSwapInterval(env_int("LINDBERGH_VSYNC", 0));   /* the game paces itself; macOS stalls vsync swaps for hidden windows */
#endif
    fprintf(stderr, "[window] GL context ready: %s / %s\n",
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    stereo_frame_begin();
    return 1;
#endif
}

void host_pump(void) { pump(); }

/* LINDBERGH_SHOT=file.ppm writes the frame numbered LINDBERGH_SHOT_FRAME (or
 * every LINDBERGH_SHOT_EVERY frames, numbered); LINDBERGH_EXIT_AFTER=N exits
 * cleanly after N frames. Both exist to check a run without watching it. */
/* LINDBERGH_PEEK=addr,len[,every]: hex dump of guest memory each `every`
 * frames (default 30), with the cabinet's buttons - for finding where the
 * game keeps what it made of its inputs. */
static void maybe_peek(void)
{
    static int on = -1; static uint32_t addr, len, every;
    if (on < 0) {
        const char *v = getenv("LINDBERGH_PEEK");
        on = v && *v;
        if (on) { char *e; addr = (uint32_t)strtoul(v, &e, 16); len = *e ? (uint32_t)strtoul(e + 1, &e, 0) : 16;
                  every = *e ? (uint32_t)strtoul(e + 1, NULL, 0) : 30; if (len > 256) len = 256; if (!every) every = 30; }
    }
    if (!on || g_frame % every) return;
    char line[1024]; int at = snprintf(line, sizeof line, "[peek] f%u btn %#x:", g_frame, g_cab.buttons);
    for (uint32_t i = 0; i < len && at < 1000; i++)
        at += snprintf(line + at, sizeof line - at, "%s%02x", i % 4 ? "" : " ", ((unsigned char *)G2H(addr))[i]);
    fprintf(stderr, "%s\n", line);
}

static unsigned g_late17, g_late25;
static double   g_worst;

static void maybe_screenshot(unsigned frame)
{
    const char *path = getenv("LINDBERGH_SHOT");
    if (!path || !*path) return;
    int every = env_int("LINDBERGH_SHOT_EVERY", 0);
    int at = env_int("LINDBERGH_SHOT_FRAME", 600);
    char name[512];
    if (every > 0) {
        if (frame == 0 || frame % (unsigned)every || frame < (unsigned)env_int("LINDBERGH_SHOT_FROM", 0)) return;
        snprintf(name, sizeof name, "%s.%05u.ppm", path, frame);
    } else {
        if (frame != (unsigned)at) return;
        snprintf(name, sizeof name, "%s", path);
    }
    int w, h;
    picture_size(&w, &h);
    /* The default framebuffer uses drawable pixels, including when the game
     * supersamples into a smaller window. Reading logical dimensions captures
     * undefined pixels outside that framebuffer. */
    GLuint from = (GLuint)env_int("LINDBERGH_SHOT_FB", 0);
    if (!from && g_win.win) SDL_GL_GetDrawableSize(g_win.win, &w, &h);
    if(w<=0 || h<=0)return;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 3);
    if (!px) return;
    GLint fb = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    /* LINDBERGH_SHOT_FB=id: one of the game's own framebuffers instead */
    glBindFramebuffer(GL_FRAMEBUFFER, from);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(from ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    FILE *f = fopen(name, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
        fclose(f);
        fprintf(stderr, "[shot] frame %u -> %s\n", frame, name);
    }
    free(px);
}

#if defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
/* Where a frame's time goes, summed over each [perf] period: the game's own
 * work (first eye included), the second eye's replay, handing both to the
 * headset, and waiting for its next frame. */
static double g_t_game, g_t_eye2, g_t_submit, g_t_wait;
static Uint64 g_t_mark;
static double since_mark(void)
{
    Uint64 now = SDL_GetPerformanceCounter();
    double ms = g_t_mark ? (now - g_t_mark) * 1000.0 / SDL_GetPerformanceFrequency() : 0;
    g_t_mark = now;
    return ms;
}
#endif

/* A game that relied on the driver's vsync to keep time (Let's Go Jungle
 * asks NVIDIA's for it through __GL_SYNC_TO_VBLANK) runs its logic once per
 * frame: unpaced it plays at whatever rate the machine reaches - ten times
 * too fast hidden, twice on a 120 Hz display. host_pace_fps(60) holds each
 * frame to its share of a second here instead; LINDBERGH_UNPACED=1 lifts it,
 * for fast test runs. */
static int g_pace_fps;
void host_pace_fps(int fps) { g_pace_fps = fps; }
static void pace(void)
{
    static int off = -1;
    if (off < 0) off = env_int("LINDBERGH_UNPACED", 0) != 0;
    if (off || g_pace_fps <= 0) return;
    static Uint64 next;
    Uint64 f = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter(), period = f / (Uint64)g_pace_fps;
    if (!next || now > next + period) next = now;      /* first frame, or fell behind: no catching up */
    while (now < next) {
        Uint64 left_ms = (next - now) * 1000 / f;
        if (left_ms > 1) SDL_Delay((Uint32)(left_ms - 1));
        now = SDL_GetPerformanceCounter();
    }
    next += period;
}

/* Shared worker: guest ABI snapshots, state filtering, one-frame overlap. */
#include "render_queue.h"

#ifndef LINDBERGH_VULKAN
static int gl_release(void) { return SDL_GL_MakeCurrent(g_win.win, NULL) == 0; }
static int gl_take(void) { return SDL_GL_MakeCurrent(g_win.win, g_win.gl) == 0; }
#endif
static void frame_gl(void *arg);
static void host_frame_done(void);

void host_swap(void)
{
    pace();
    {   /* LINDBERGH_RESIZE_AT=frame,WxH: resize the window then - a test */
        static int at = -1, rw, rh;
        if (at < 0) { const char *v = getenv("LINDBERGH_RESIZE_AT"); at = 0;
                      if (v) sscanf(v, "%d,%dx%d", &at, &rw, &rh); }
        if (at && g_frame == (unsigned)at && g_win.win) SDL_SetWindowSize(g_win.win, rw, rh);
    }
    {   /* LINDBERGH_GL_THREAD=1: the GL on a thread of its own (gl_thread.c),
         * from this first swap on */
        static int want = -1;
        if (want < 0) want = env_int("LINDBERGH_GL_THREAD", 0);   /* or the game's profile: vr.gl_thread */
#ifdef HOTD4_DESKTOP_PAUSE
        if (hotd4_pause_enabled()) want = 0;
#endif
        if (want == 1) {
#ifdef LINDBERGH_VULKAN
            if (lvk_host_can_thread()) {
                want = 2;
                glq_set_name_source(lvk_reserve_names, lvk_adopt_buffer_names);
                glq_start(NULL, lvk_host_thread_enter);
            }
#else
            if (g_win.gl) { want = 2; glq_start(gl_release, gl_take); }
#endif
        }
    }
    if (glq_active()) {
        /* the end of this frame goes to the worker; the game waits only for
         * the frame before, so it runs at most one frame ahead */
        static uint32_t prev;
        uint32_t job = glq_job(frame_gl, NULL);
        glq_wait(prev);
        prev = job;
    } else frame_gl(NULL);
    host_frame_done();
}

void host_viewport_scale(int *x, int *y, int *w, int *h)
{
    GLint fb = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    if (fb != 0) return;

    if (!g_win.win || g_win.w <= 0 || g_win.h <= 0) return;

    int draw_w = 0, draw_h = 0;
    SDL_GL_GetDrawableSize(g_win.win, &draw_w, &draw_h);
    if (draw_w <= 0 || draw_h <= 0) return;

    GameViewport viewport = fit_game_viewport(draw_w, draw_h);
    int vp_x = viewport.x, vp_y = viewport.y, vp_w = viewport.w, vp_h = viewport.h;
    g_vp_x = vp_x;
    g_vp_y = vp_y;
    g_vp_w = vp_w;
    g_vp_h = vp_h;

    if (g_guest_viewport_coords || (*w >= 320 && *h >= 240)) {
        *x = vp_x + (int)((long)*x * vp_w / g_win.w);
        *y = vp_y + (int)((long)*y * vp_h / g_win.h);
        *w = (int)((long)*w * vp_w / g_win.w);
        *h = (int)((long)*h * vp_h / g_win.h);
    }
}

void host_viewport_unscale(int rect[4])
{
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    if (framebuffer || g_vp_w <= 0 || g_vp_h <= 0 || g_win.w <= 0 || g_win.h <= 0) return;
    rect[0] = (int)lround((rect[0] - g_vp_x) * (double)g_win.w / g_vp_w);
    rect[1] = (int)lround((rect[1] - g_vp_y) * (double)g_win.h / g_vp_h);
    rect[2] = (int)lround(rect[2] * (double)g_win.w / g_vp_w);
    rect[3] = (int)lround(rect[3] * (double)g_win.h / g_vp_h);
}

static void draw_4k_crosshair(float cx, float cy, float scale, int reloading)
{
    /* Align the edges to the drawable's pixels. Keep the aiming point at
     * the same position as the regular mouse overlay. */
    cx = floorf(cx) + 0.5f;
    cy = floorf(cy) + 0.5f;
    float gap = roundf(7.0f * scale);
    float length = roundf(18.0f * scale);
    float stroke = roundf(2.0f * scale) * 0.5f;
    float outline = roundf(scale);
    float bars[][4] = {
        {cx - length, cy - stroke, cx - gap, cy + stroke},
        {cx + gap, cy - stroke, cx + length, cy + stroke},
        {cx - stroke, cy - length, cx + stroke, cy - gap},
        {cx - stroke, cy + gap, cx + stroke, cy + length},
        {cx - stroke, cy - stroke, cx + stroke, cy + stroke}
    };

    glDisable(GL_POLYGON_SMOOTH);
    glColor4ub(0, 0, 0, 255);
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        const float *r = bars[i];
        glRectf(floorf(r[0]) - outline, floorf(r[1]) - outline,
                floorf(r[2]) + outline, floorf(r[3]) + outline);
    }
    if (reloading) glColor4ub(255, 76, 76, 255);
    else glColor4ub(236, 255, 255, 255);
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        const float *r = bars[i];
        glRectf(floorf(r[0]), floorf(r[1]), floorf(r[2]), floorf(r[3]));
    }
}

#ifdef HOTD4_DESKTOP_PAUSE
static void red_crosshair(float cx, float cy, float scale)
{
    const float r = 22.0f * scale;
    const float stroke = 1.05f * scale;
    crosshair_color_apply(249, 38, 51, 255);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 96; ++i) {
        float angle = (float)i * (6.2831853f / 96.0f);
        float ux = cosf(angle), uy = sinf(angle);
        glVertex2f(cx + ux * (r-stroke), cy + uy * (r-stroke));
        glVertex2f(cx + ux * (r+stroke), cy + uy * (r+stroke));
    }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 24; ++i) {
        float angle = (float)i * (6.2831853f / 24.0f);
        glVertex2f(cx + cosf(angle) * 1.85f * scale, cy + sinf(angle) * 1.85f * scale);
    }
    glEnd();

    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            float dx = sx * 0.70710678f, dy = sy * 0.70710678f;
            float a = 13.6f * scale, b = 27.5f * scale, thick = 1.65f * scale;
            glBegin(GL_TRIANGLES);
            glVertex2f(cx + dx*a, cy + dy*a);
            glVertex2f(cx + dx*b - dy*thick, cy + dy*b + dx*thick);
            glVertex2f(cx + dx*b + dy*thick, cy + dy*b - dx*thick);
            glEnd();
            float start = 29.2f * scale, end = 32.6f * scale;
            thick = 2.45f * scale;
            glBegin(GL_QUADS);
            glVertex2f(cx + dx*start - dy*thick, cy + dy*start + dx*thick);
            glVertex2f(cx + dx*end - dy*thick, cy + dy*end + dx*thick);
            glVertex2f(cx + dx*end + dy*thick, cy + dy*end - dx*thick);
            glVertex2f(cx + dx*start + dy*thick, cy + dy*start - dx*thick);
            glEnd();
        }
    }
}
#endif

static void draw_player_crosshair(int win_w, int win_h, int player)
{
    if (!settings()->crosshair || win_w <= 0 || win_h <= 0 || g_win.w <= 0 || g_win.h <= 0) return;
    int mx=player?g_gun2_px:g_mouse_x, my=player?g_gun2_py:g_mouse_y;
    if (mx < 0 || my < 0 || mx >= g_win.w || my >= g_win.h) return;
    crosshair_drawing_player=player;

    int vp_w = g_vp_w > 0 ? g_vp_w : win_w;
    int vp_h = g_vp_h > 0 ? g_vp_h : win_h;
    float cx = (float)g_vp_x + ((float)mx + 0.5f) * (float)vp_w / (float)g_win.w;
    float cy = (float)(win_h - g_vp_y - vp_h) + ((float)my + 0.5f) * (float)vp_h / (float)g_win.h;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    if (glUseProgram) glUseProgram(0);
#ifdef GL_VERTEX_PROGRAM_ARB
    if (glBindProgramARB) {
        glBindProgramARB(GL_VERTEX_PROGRAM_ARB, 0);
        glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
    }
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
#endif

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    if (glBindBuffer) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    for (int i = 7; i >= 0; i--) {
        glActiveTexture(GL_TEXTURE0 + i);
        glDisable(GL_TEXTURE_2D);
    }

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)win_w, (double)win_h, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    float base_scale = (float)win_h / 1080.0f;
    if (base_scale < 1.0f) base_scale = 1.0f;
    else if (base_scale > 2.2f) base_scale = 2.2f;

#ifdef HOTD4_DESKTOP_PAUSE
    if (hotd4_pause_enabled()) {
        float reticle_scale=base_scale*crosshair_size_get()/100.0f;
        if(!extra_crosshair_draw(cx, cy, reticle_scale))red_crosshair(cx, cy, reticle_scale);
        goto restore_crosshair_state;
    }
#endif

    float r_outer = 13.0f * base_scale;
    float r_gap   = 16.0f * base_scale;
    float r_tick  = 23.0f * base_scale;
    float dot_r   = 2.2f * base_scale;

    int is_reloading = (host_cabinet_buttons() & (player?CAB_P2_RELOAD:CAB_P1_RELOAD)) != 0;

    float hud_scale = hud_custom_scale();
    if (hud_scale > 0) {
        draw_4k_crosshair(cx, cy, base_scale * hud_scale, is_reloading);
        goto restore_crosshair_state;
    }

    /* 1. Black outer outline */
    glColor4f(0.0f, 0.0f, 0.0f, 0.9f);
    glLineWidth(3.6f * base_scale);

    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 32; i++) {
        float theta = 2.0f * 3.1415926535f * (float)i / 32.0f;
        glVertex2f(cx + r_outer * cosf(theta), cy + r_outer * sinf(theta));
    }
    glEnd();

    glBegin(GL_LINES);
    glVertex2f(cx - r_tick, cy); glVertex2f(cx - r_gap, cy);
    glVertex2f(cx + r_gap, cy);  glVertex2f(cx + r_tick, cy);
    glVertex2f(cx, cy - r_tick); glVertex2f(cx, cy - r_gap);
    glVertex2f(cx, cy + r_gap);  glVertex2f(cx, cy + r_tick);
    glEnd();

    /* 2. Inner colored reticle */
    if (is_reloading) {
        glColor4f(1.0f, 0.2f, 0.2f, 0.95f);
    } else {
        glColor4f(0.0f, 0.95f, 1.0f, 0.95f);
    }
    glLineWidth(1.8f * base_scale);

    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 32; i++) {
        float theta = 2.0f * 3.1415926535f * (float)i / 32.0f;
        glVertex2f(cx + r_outer * cosf(theta), cy + r_outer * sinf(theta));
    }
    glEnd();

    glBegin(GL_LINES);
    glVertex2f(cx - r_tick, cy); glVertex2f(cx - r_gap, cy);
    glVertex2f(cx + r_gap, cy);  glVertex2f(cx + r_tick, cy);
    glVertex2f(cx, cy - r_tick); glVertex2f(cx, cy - r_gap);
    glVertex2f(cx, cy + r_gap);  glVertex2f(cx, cy + r_tick);
    glEnd();

    /* 3. Center dot with black border */
    glColor4f(0.0f, 0.0f, 0.0f, 0.9f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 16; i++) {
        float theta = 2.0f * 3.1415926535f * (float)i / 16.0f;
        glVertex2f(cx + (dot_r + 1.2f * base_scale) * cosf(theta), cy + (dot_r + 1.2f * base_scale) * sinf(theta));
    }
    glEnd();

    if (is_reloading) {
        glColor4f(1.0f, 0.3f, 0.3f, 1.0f);
    } else {
        glColor4f(0.3f, 1.0f, 0.3f, 1.0f);
    }
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 16; i++) {
        float theta = 2.0f * 3.1415926535f * (float)i / 16.0f;
        glVertex2f(cx + dot_r * cosf(theta), cy + dot_r * sinf(theta));
    }
    glEnd();

restore_crosshair_state:
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glPopClientAttrib();
    glPopAttrib();
}

static void draw_crosshair_overlay(int win_w, int win_h)
{
    draw_player_crosshair(win_w,win_h,0);
    if (g_gun2_on) draw_player_crosshair(win_w,win_h,1);
    crosshair_drawing_player=0;
}

#if defined(HOTD4_DESKTOP_PAUSE)
#include "sinden_border.h"
#include "photo_mode.h"
#include "photo_camera.h"
#include "photo_filters.h"

enum { PAUSE_UNAVAILABLE = -1, RESUME, RESTART, SETTINGS, ACHIEVEMENTS, HIGH_SCORES, PHOTO_MODE, MAIN_MENU };
enum { PANEL_W = 600, PANEL_H = 554, ROW_TOP = 118, ROW_STEP = 54, ROW_HEIGHT = 45 };

typedef struct {
    int settings_page, selected;
    int binding_capture;wchar_t binding_message[200];
    int trophy_filter,trophy_scroll,trophy_drag;float trophy_drag_offset;
    GLuint frame, panel;
    int photo, photo_guide, photo_status, photo_ready, photo_drag, photo_help;
    PhotoCamera camera;
    GLuint photo_frame, photo_fbo;
    PhotoFilter filter;
    PhotoFilterPass filter_pass;
    int filter_dirty;
    int frame_width, frame_height;
} PauseMenu;

static int (*photo_camera_available)(void);
static int (*photo_render_scene)(const PhotoCamera *);

void hotd4_photo_register(int (*available)(void), int (*render)(const PhotoCamera *))
{
    photo_camera_available = available;
    photo_render_scene = render;
}

static const int hud_percent[] = {0, 75, 100, 125};

static const wchar_t *achievement_names[12];
static const wchar_t *achievement_descriptions[12];
static GLuint achievement_star_texture(int metal);
static int achievement_is_unlocked(int id);
static int achievement_count(void);
static int menu_row_count(const PauseMenu *menu) { return menu->settings_page==6?3:menu->settings_page==1?(hotd4_special_menu()?9:10):menu->settings_page==2?9:hotd4_special_menu()?4:7; }

static int menu_step(const PauseMenu *menu){return menu->settings_page==2?42:menu->settings_page==1?38:menu->settings_page==6?42:ROW_STEP;}
static int menu_height(const PauseMenu *menu){return menu->settings_page==2?36:menu->settings_page==1?32:menu->settings_page==6?36:ROW_HEIGHT;}

static int pause_row_at(const PauseMenu *menu, SDL_Window *window, int x, int y)
{
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    float scale = fminf(ww / 960.0f, wh / 650.0f);
    if (scale <= 0) return -1;
    float panel_x = (ww - (PANEL_W + (menu->settings_page == 1 ? 260 : 0)) * scale) * 0.5f + (menu->settings_page == 1 ? 260 * scale : 0);
    float px = (x - panel_x) / scale;
    float py = (y - (wh - PANEL_H * scale) * 0.5f) / scale - ROW_TOP;
    if (px < 31 || px >= PANEL_W - 31 || py < 0) return -1;
    int row = (int)py / menu_step(menu);
    return row < menu_row_count(menu) && (int)py % menu_step(menu) < menu_height(menu) ? row : -1;
}

static void pause_fill(HDC dc, int x, int y, int w, int h, COLORREF color)
{
    RECT box = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) { FillRect(dc, &box, brush); DeleteObject(brush); }
}

static void pause_text(HDC dc, HFONT font, int x, int y, const wchar_t *label, COLORREF color)
{
    SelectObject(dc, font);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, label, (int)wcslen(label));
}

#include "trophy_gallery.h"
#include "controls_menu.h"
#include "high_scores_ui.h"

static int photo_guide_height(const PauseMenu *menu)
{
    return menu->photo_help ? PHOTO_GUIDE_EXPANDED : PHOTO_GUIDE_COMPACT;
}

static int pause_panel(PauseMenu *menu)
{
    if(menu->settings_page==5)return high_scores_panel(menu);
    if(menu->settings_page==4)return controls_panel(menu);
    if(menu->settings_page==3)return trophy_gallery_panel(menu);
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = PANEL_W;
    int panel_height = menu->photo ? photo_guide_height(menu) : PANEL_H;
    info.bmiHeader.biHeight = -panel_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;

    void *pixels = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return 0;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    HFONT title = CreateFontW(-45, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT item = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT hint = CreateFontW(-17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             ANTIALIASED_QUALITY, 0, L"Segoe UI");
    int valid = bitmap && pixels && title && item && hint;
    if (valid) {
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        HGDIOBJ old_font = GetCurrentObject(dc, OBJ_FONT);
        const COLORREF white = RGB(243, 240, 234), muted = RGB(201, 191, 190);
        SetBkMode(dc, TRANSPARENT);
        if (menu->photo) {
            pause_fill(dc, 0, 0, PANEL_W, panel_height, RGB(17, 17, 21));
            pause_fill(dc, 0, 0, PANEL_W, 3, RGB(183, 56, 53));
            const wchar_t *status = menu->photo_status > 0 ? L"Saved to Photos" :
                                    menu->photo_status == -1 ? L"Could not save the photo" :
                                    menu->photo_status == -2 ? L"Filter unavailable" : L"Photo Mode";
            pause_text(dc, item, 18, 10, status, white);

            wchar_t filter_label[64];
            swprintf(filter_label, 64, L"F   %ls", photo_filter_names[menu->filter]);
            SIZE filter_size;
            SelectObject(dc, hint);
            if (GetTextExtentPoint32W(dc, filter_label, (int)wcslen(filter_label), &filter_size))
                pause_text(dc, hint, PANEL_W - 18 - filter_size.cx, 17, filter_label, RGB(245, 151, 131));

            if (menu->photo_help) {
                pause_fill(dc, 18, 40, PANEL_W - 36, 1, RGB(65, 62, 70));
                if (menu->photo_ready) {
                    pause_text(dc, hint, 18, 50, L"CAMERA", white);
                    pause_text(dc, hint, 18, 73, L"Right drag  Look     Wheel  Zoom", muted);
                    pause_text(dc, hint, 18, 96, L"WASD  Move     Q/E  Height", muted);
                    pause_text(dc, hint, 310, 50, L"COMPOSITION", white);
                    pause_text(dc, hint, 310, 73, L"Shift + right drag / Z/C  Roll", muted);
                    pause_text(dc, hint, 310, 96, L"H  HUD     R  Reset camera", muted);
                } else {
                    pause_text(dc, hint, 18, 75, L"Free camera unavailable in this scene", muted);
                }
            }
            pause_text(dc, hint, 18, panel_height - 27, menu->photo_help ?
                       L"Enter   Save       Tab   Hide       F1   Close controls       Esc   Back" :
                       L"Enter   Save       Tab   Hide       F1   Controls       Esc   Back", muted);
        } else {
            pause_fill(dc, 0, 0, PANEL_W, PANEL_H, RGB(17, 17, 21));
            pause_fill(dc, 0, 0, PANEL_W, 3, RGB(183, 56, 53));
            pause_text(dc, title, 31, 30, menu->settings_page==6?L"Gameplay":menu->settings_page == 3 ? L"Trophies" : menu->settings_page ? L"Settings" : L"Paused", white);

            wchar_t hud[64], reticle[64];
            int value = (int)lroundf(hud_custom_scale() * 100);
            if (value) swprintf(hud, 64, L"HUD Scale     < %d%% >", value);
            else wcscpy(hud, L"HUD Scale     < Original >");
            swprintf(reticle, 64, L"Crosshair     < %ls >", settings()->crosshair ? L"On" : L"Off");
            const wchar_t *main_items[] = {L"Resume", getenv("HOTD4_PRACTICE_CHAPTER") ?
                                          L"Restart Chapter" : L"Restart Arcade", L"Settings", L"Trophies", L"High Scores", L"Photo Mode", L"Main Menu"};
            const wchar_t *special_items[]={L"Resume",getenv("HOTD4SP_CHAPTER")?L"Restart Chapter":L"Restart Arcade",L"Settings",L"Main Menu"};
            wchar_t sinden[64];
            swprintf(sinden, 64, L"Sinden Border     < %ls >", sinden_border_enabled() ? L"On" : L"Off");
            const wchar_t *styles[] = {L"Original Red",L"Blue Circle",L"Red Square",L"Name Entry (Original)"};
            wchar_t style[80];
            swprintf(style,80,L"Crosshair Style  < %ls >",styles[crosshair_style_get()]);
            wchar_t size[64];
            swprintf(size,64,L"Crosshair Size   < %d%% >",crosshair_size_get());
            wchar_t color[64];
            swprintf(color,64,L"Crosshair Color  < %ls >",crosshair_color_names[crosshair_color_get()]);
            wchar_t border_width[96];swprintf(border_width,96,L"Border Width   < %ls >",sinden_width_names[sinden_border_width_index()]);
            wchar_t score_display[96];swprintf(score_display,96,L"High Score Display  < %ls >",high_scores_enabled()?L"On":L"Off");
            const wchar_t *settings_items[] = {hud, reticle, style, size, color, sinden, border_width, score_display, L"Gameplay...", L"Input & Anti-Aliasing..."};
            const wchar_t *special_settings[]={hud,reticle,style,size,color,sinden,border_width,L"Gameplay...",L"Input & Anti-Aliasing..."};
            wchar_t mode[96],gun1[128],gun2[128],aa[80],mouse_speed[80],smoothing[80];
            swprintf(mode,96,L"Input  < %ls >",input_mode_names[input_pref_mode]);
            input_gun_label(0,gun1,128);input_gun_label(1,gun2,128);
            if(input_pref_aa)swprintf(aa,80,L"Anti-Aliasing  < MSAA %dx >",input_pref_aa);
            else wcscpy(aa,L"Anti-Aliasing  < Off >");
            swprintf(mouse_speed,80,L"Mouse Speed  < %.2fx >", (double)input_mouse_speed);
            swprintf(smoothing,80,L"Mouse Smoothing  < %ls >", mouse_smoothing_names[input_mouse_smoothing]);
            const wchar_t *input_items[]={mode,gun1,gun2,aa,mouse_speed,smoothing,L"Key Bindings...",L"Apply & Restart",L"Back"};
            wchar_t offscreen[96],blood[96];
            swprintf(offscreen,96,L"Lightgun Off-Screen Reload  < %ls >",gameplay_offscreen_reload()?L"On":L"Off");
            swprintf(blood,96,L"Blood Splatter  < %ls >",gameplay_blood_splatter()?L"Enhanced":L"Original");
            const wchar_t *gameplay_items[]={offscreen,blood,L"Back"};
            const wchar_t *special_gameplay[]={offscreen,L"Back"};
            const wchar_t **items=menu->settings_page==6?gameplay_items:menu->settings_page==2?input_items:menu->settings_page?(hotd4_special_menu()?special_settings:settings_items):(hotd4_special_menu()?special_items:main_items);
            for (int i = 0; i < menu_row_count(menu); ++i) {
                int y = ROW_TOP + i * menu_step(menu);
                int selected = i == menu->selected;
                pause_fill(dc, 31, y, PANEL_W - 62, menu_height(menu),
                           selected ? RGB(116, 34, 39) : RGB(38, 38, 45));
                if (selected) pause_fill(dc, 31, y, 3, menu_height(menu), RGB(245, 115, 93));
                int available = menu->settings_page != 2 ||
                    (i==1 ? input_pref_mode>=2 : i==2 ? input_pref_mode==2 :
                     (i==4 || i==5) ? (input_pref_mode==1 || input_pref_mode==4) : 1);
                SelectObject(dc,item);
                SetTextColor(dc,available ? white : muted);
                RECT text_box={49,y,PANEL_W-45,y+menu_height(menu)};
                DrawTextW(dc,items[i],-1,&text_box,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
            }
            pause_text(dc, hint, 32, 515, menu->settings_page == 2 ?
                       input_pref_mode>=3 ? L"P1: gun buttons. P2: mouse / shooting keys. Restart to apply." :
                       L"Mouse tuning is live. Restart to apply input mode or MSAA." : menu->settings_page ?
                       L"Arrow keys to adjust     Enter to select     Esc to go back" :
                       L"Arrow keys or mouse     Enter to select     Esc to resume", muted);
        }
        GdiFlush();

        glBindTexture(GL_TEXTURE_2D, menu->panel);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, PANEL_W, panel_height, 0, GL_BGRA,
                     GL_UNSIGNED_BYTE, pixels);
        SelectObject(dc, old_font);
        SelectObject(dc, old_bitmap);
    }
    if (hint) DeleteObject(hint);
    if (item) DeleteObject(item);
    if (title) DeleteObject(title);
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(dc);
    return valid;
}

static void pause_texture_setup(GLuint texture)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static int photo_surface_create(GLuint *texture, GLuint *fbo, int width, int height)
{
    if (*fbo) return 1;
    GLint previous;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glGenTextures(1, texture);
    glGenFramebuffers(1, fbo);
    pause_texture_setup(*texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    int ready = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous);
    if (!ready) {
        glDeleteFramebuffers(1, fbo);
        glDeleteTextures(1, texture);
        *fbo = *texture = 0;
    }
    return ready;
}

static int photo_filter_render(PauseMenu *menu)
{
    PhotoFilterPass *pass = &menu->filter_pass;
    if (!pass->program) {
        pass->program = photo_filter_program();
        if (!pass->program) return 0;
        pass->mode = glGetUniformLocation(pass->program, "mode");
        pass->size = glGetUniformLocation(pass->program, "size");
    }
    int width = menu->frame_width, height = menu->frame_height;
    if (!photo_surface_create(&pass->texture, &pass->framebuffer, width, height)) return 0;

    GLint previous, program;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pass->framebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, width, height);
    glDisable(GL_DITHER);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, menu->photo_ready ? menu->photo_frame : menu->frame);
    glUseProgram(pass->program);
    /* The program's single sampler uses texture unit zero. */
    glUniform1i(pass->mode, menu->filter);
    glUniform2f(pass->size, (float)width, (float)height);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glUseProgram((GLuint)program);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous);
    glPopAttrib();
    return 1;
}

static int photo_refresh(PauseMenu *menu, const PhotoCamera *camera)
{
    int w, h;
    SDL_GL_GetDrawableSize(g_win.win, &w, &h);
    if (w != menu->frame_width || h != menu->frame_height ||
        !photo_surface_create(&menu->photo_frame, &menu->photo_fbo, w, h))
        return 0;

    GLint program, pack, unpack;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_TEXTURE); glPushMatrix();
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(~0u);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    int drawn = photo_render_scene(camera);
    if (drawn) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, menu->photo_fbo);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        menu->filter_dirty = 1;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack);
    glUseProgram((GLuint)program);
    return drawn;
}

enum { PHOTO_DRAG_OFF, PHOTO_DRAG_RELATIVE, PHOTO_DRAG_CAPTURED, PHOTO_DRAG_WINDOW };

static void photo_stop_drag(PauseMenu *menu)
{
    if (menu->photo_drag == PHOTO_DRAG_RELATIVE) SDL_SetRelativeMouseMode(SDL_FALSE);
    if (menu->photo_drag == PHOTO_DRAG_CAPTURED) SDL_CaptureMouse(SDL_FALSE);
    menu->photo_drag = PHOTO_DRAG_OFF;
}

static void photo_start_drag(PauseMenu *menu)
{
    if (menu->photo_drag) return;
    if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) menu->photo_drag = PHOTO_DRAG_RELATIVE;
    else if (SDL_CaptureMouse(SDL_TRUE) == 0) menu->photo_drag = PHOTO_DRAG_CAPTURED;
    else menu->photo_drag = PHOTO_DRAG_WINDOW;
}

static void photo_look(PhotoCamera *camera, float yaw, float pitch)
{
    const float turn = 6.2831853f;
    camera->yaw = remainderf(camera->yaw + yaw, turn);
    camera->pitch = fminf(1.48f, fmaxf(-1.48f, camera->pitch + pitch));
}

static void photo_roll(PhotoCamera *camera, float angle)
{
    camera->roll = remainderf(camera->roll + angle, 6.2831853f);
}

static int photo_pointer(PhotoCamera *camera, const SDL_Event *event, int dragging)
{
    if (event->type == SDL_MOUSEWHEEL) {
        float steps = event->wheel.preciseY != 0 ? event->wheel.preciseY : (float)event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) steps = -steps;
        float zoom = fminf(2.0f, fmaxf(-1.0f, camera->zoom_stops + steps * 0.20f));
        if (zoom == camera->zoom_stops) return 0;
        camera->zoom_stops = zoom;
        return 1;
    }
    if (event->type != SDL_MOUSEMOTION || !dragging) return 0;

    float x = (float)event->motion.xrel, y = (float)event->motion.yrel;
    SDL_Event next;
    // Apply adjacent mouse packets in one redraw. Leave clicks, wheel events
    // and keys in order so saving always uses the requested camera pose.
    while (SDL_PeepEvents(&next, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1 &&
           next.type == SDL_MOUSEMOTION && next.motion.windowID == event->motion.windowID &&
           SDL_PeepEvents(&next, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) == 1) {
        x += (float)next.motion.xrel;
        y += (float)next.motion.yrel;
    }
    if (SDL_GetModState() & KMOD_SHIFT) {
        if (x == 0) return 0;
        photo_roll(camera, x * 0.004f);
    } else {
        if (x == 0 && y == 0) return 0;
        photo_look(camera, x * 0.003f, -y * 0.003f);
    }
    return 1;
}

static int photo_move(PhotoCamera *camera, SDL_Keycode key)
{
    const float step = 0.40f, turn = 0.035f;
    float right = 0, up = 0, forward = 0;
    switch (key) {
    case SDLK_w: forward = step; break;
    case SDLK_s: forward = -step; break;
    case SDLK_d: right = step; break;
    case SDLK_a: right = -step; break;
    case SDLK_e: up = step; break;
    case SDLK_q: up = -step; break;
    case SDLK_RIGHT: photo_look(camera, turn, 0); return 1;
    case SDLK_LEFT: photo_look(camera, -turn, 0); return 1;
    case SDLK_UP: photo_look(camera, 0, turn); return 1;
    case SDLK_DOWN: photo_look(camera, 0, -turn); return 1;
    case SDLK_z: photo_roll(camera, -turn); return 1;
    case SDLK_c: photo_roll(camera, turn); return 1;
    default: return 0;
    }
    float cy = cosf(camera->yaw), sy = sinf(camera->yaw), cp = cosf(camera->pitch);
    camera->position[0] += cy * right + sy * cp * forward;
    camera->position[1] += up + sinf(camera->pitch) * forward;
    camera->position[2] += sy * right - cy * cp * forward;
    return 1;
}

static void pause_quad(float x, float y, float w, float h, int flip)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, flip); glVertex2f(x, y);
    glTexCoord2f(1, flip); glVertex2f(x+w, y);
    glTexCoord2f(1, 1-flip); glVertex2f(x+w, y+h);
    glTexCoord2f(0, 1-flip); glVertex2f(x, y+h);
    glEnd();
}

static void pause_draw(PauseMenu *menu)
{
    if (menu->photo && menu->filter_dirty) {
        menu->filter_dirty = 0;
        if (menu->filter != PHOTO_FILTER_ORIGINAL && !photo_filter_render(menu)) {
            menu->filter = PHOTO_FILTER_ORIGINAL;
            menu->photo_status = -2;
            pause_panel(menu);
        }
    }
    int w, h;
    SDL_GL_GetDrawableSize(g_win.win, &w, &h);
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_TEXTURE_2D);
    GLuint picture = menu->frame;
    if (menu->photo) {
        if (menu->filter != PHOTO_FILTER_ORIGINAL) picture = menu->filter_pass.texture;
        else if (menu->photo_ready) picture = menu->photo_frame;
    }
    glBindTexture(GL_TEXTURE_2D, picture);
    if (menu->photo) {
        float scale = fminf((float)w / menu->frame_width, (float)h / menu->frame_height);
        float width = menu->frame_width * scale, height = menu->frame_height * scale;
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor3f(1, 1, 1);
        pause_quad((w - width) / 2, (h - height) / 2, width, height, 1);
        if (menu->photo_guide) {
            float guide_scale = fminf(w / 960.0f, h / 650.0f);
            float guide_width = PANEL_W * guide_scale, guide_height = photo_guide_height(menu) * guide_scale;
            glBindTexture(GL_TEXTURE_2D, menu->panel);
            pause_quad((w - guide_width) / 2, h - guide_height - 18 * guide_scale,
                       guide_width, guide_height, 0);
        }
        SDL_GL_SwapWindow(g_win.win);
        return;
    }
    glColor3f(0.42f, 0.42f, 0.42f);
    pause_quad(0, 0, w, h, 1);
    if(menu->settings_page==5){high_scores_draw(menu,w,h);sinden_border_draw(w,h);SDL_GL_SwapWindow(g_win.win);return;}
    if(menu->settings_page==4){controls_draw(menu,w,h);sinden_border_draw(w,h);SDL_GL_SwapWindow(g_win.win);return;}
    if(menu->settings_page==3){
        trophy_gallery_draw(menu,w,h);sinden_border_draw(w,h);SDL_GL_SwapWindow(g_win.win);return;
    }
    float scale = fminf(w / 960.0f, h / 650.0f);
    glBindTexture(GL_TEXTURE_2D, menu->panel);
    glColor3f(1, 1, 1);
    float left = (w - (PANEL_W + (menu->settings_page == 1 ? 260 : 0))*scale)/2;
    pause_quad(left + (menu->settings_page == 1 ? 260*scale : 0), (h - PANEL_H*scale)/2,
               PANEL_W*scale, PANEL_H*scale, 0);
    if(menu->settings_page==1){
        float x=left,y=h*.5f-130*scale;
        glDisable(GL_TEXTURE_2D);
        glColor4ub(80,84,94,255);glRectf(x,y,x+240*scale,y+260*scale);
        glColor4ub(16,18,24,255);glRectf(x+scale,y+scale,x+239*scale,y+259*scale);
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        float size=fminf(2.2f,fmaxf(1.0f,h/1080.0f))*crosshair_size_get()/100.0f;
        if(!extra_crosshair_draw(x+120*scale,h*.5f,size))red_crosshair(x+120*scale,h*.5f,size);
        glDisable(GL_BLEND);
    }
    sinden_border_draw(w, h);
    SDL_GL_SwapWindow(g_win.win);
}

static void pause_set_hud(int delta)
{
    int index = 0;
    int current = (int)lroundf(hud_custom_scale() * 100);
    for (int i = 0; i < 4; i++) if (hud_percent[i] == current) index = i;
    int value = hud_percent[(index+delta+4)%4];
    hud_set_custom_scale(value / 100.0f);
    const char *path = getenv("HOTD4_HUD_PROFILE");
    if (path && *path) {
        char temp[1024];
        int len = snprintf(temp, sizeof temp, "%s.pause-%lu.tmp", path, (unsigned long)GetCurrentProcessId());
        if (len < 0 || len >= (int)sizeof temp) return;
        FILE *file = fopen(temp, "w");
        if (file) {
            int valid = fprintf(file, "%d\n", value) >= 0;
            if (fclose(file) != 0) valid = 0;
            if (valid && MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING)) return;
            DeleteFileA(temp);
        }
        fprintf(stderr, "[pause] Could not save the HUD profile\n");
    }
}

/* Hold the game thread at the frame boundary. Only SDL events and the frozen
 * picture are processed until a menu action returns. */
static int hotd4_pause_loop(int w, int h)
{
    if (w <= 0 || h <= 0) return PAUSE_UNAVAILABLE;
    PauseMenu menu = {0};menu.binding_capture=-1;
    menu.frame_width = w;
    menu.frame_height = h;
    GLint program, read_fbo, draw_fbo, unpack;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST); glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glDisable(GL_BLEND); glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_VERTEX_PROGRAM_ARB); glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    for (int i = 7; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0+i);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_CUBE_MAP);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    GLuint copy_fbo = 0;
    glGenTextures(1, &menu.frame);
    glGenTextures(1, &menu.panel);
    pause_texture_setup(menu.frame);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &copy_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copy_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, menu.frame, 0);
    int ready = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ready) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    pause_texture_setup(menu.panel);
    ready = ready && pause_panel(&menu);

    int action = PAUSE_UNAVAILABLE;
    if (ready) {
        action = RESUME;
        SDL_bool relative = SDL_GetRelativeMouseMode();
        SDL_bool grab = SDL_GetWindowGrab(g_win.win);
        int cursor = SDL_ShowCursor(SDL_QUERY);
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_SetWindowGrab(g_win.win, SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        raw_suspended=1;raw_release();
        host_guest_clock_pause(1);
        host_audio_pause_devices(1);
        fprintf(stderr, "[pause] opened at frame %u\n", g_frame);
        pause_draw(&menu);
        for (;;) {
            SDL_Event event;
            if (!SDL_WaitEventTimeout(&event, 50)) continue;
            int activate = 0, delta = 0, redraw = 0;
            if (event.type == SDL_QUIT) { action = MAIN_MENU; break; }
            if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED)
                pad_event(&event);
            if (menu.photo) {
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT && menu.photo_ready) {
                    photo_start_drag(&menu);
                    continue;
                }
                if ((event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) ||
                    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
                    photo_stop_drag(&menu);
                    continue;
                }
                if (menu.photo_ready && (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL)) {
                    PhotoCamera camera = menu.camera;
                    if (photo_pointer(&camera, &event, menu.photo_drag) && photo_refresh(&menu, &camera)) {
                        menu.camera = camera;
                        if (menu.photo_status) {
                            menu.photo_status = 0;
                            pause_panel(&menu);
                        }
                        pause_draw(&menu);
                    }
                    continue;
                }
                if (event.type == SDL_KEYDOWN) {
                    SDL_Keycode key = event.key.keysym.sym;
                    if (!event.key.repeat && key == SDLK_ESCAPE) {
                        photo_stop_drag(&menu);
                        if (menu.photo_ready) {
                            const PhotoCamera original = {0};
                            photo_refresh(&menu, &original);
                        }
                        menu.photo = 0;
                        SDL_ShowCursor(SDL_ENABLE);
                    } else if (!event.key.repeat && key == SDLK_TAB) {
                        menu.photo_guide = !menu.photo_guide;
                    } else if (!event.key.repeat && key == SDLK_F1) {
                        menu.photo_help = !menu.photo_help;
                        menu.photo_guide = 1;
                    } else if (!event.key.repeat && key == SDLK_f) {
                        int step = (event.key.keysym.mod & KMOD_SHIFT) ? PHOTO_FILTER_COUNT - 1 : 1;
                        menu.filter = (menu.filter + step) % PHOTO_FILTER_COUNT;
                        menu.filter_dirty = 1;
                        menu.photo_status = 0;
                    } else if (!event.key.repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
                        GLuint capture = menu.photo_ready ? menu.photo_fbo : copy_fbo;
                        if (menu.filter != PHOTO_FILTER_ORIGINAL)
                            capture = menu.filter_pass.framebuffer;
                        menu.photo_status = photo_save(capture, w, h) ? 1 : -1;
                        menu.photo_guide = 1;
                    } else if (menu.photo_ready) {
                        PhotoCamera camera = menu.camera;
                        if (!event.key.repeat && key == SDLK_h) {
                            camera.hide_hud = !camera.hide_hud;
                        } else if (!event.key.repeat && key == SDLK_r) {
                            camera = (PhotoCamera){.hide_hud = menu.camera.hide_hud};
                        } else if (!photo_move(&camera, key)) {
                            continue;
                        }
                        if (photo_refresh(&menu, &camera)) menu.camera = camera;
                        menu.photo_status = 0;
                    } else {
                        continue;
                    }
                    pause_panel(&menu);
                    pause_draw(&menu);
                } else if (event.type == SDL_WINDOWEVENT) {
                    pause_draw(&menu);
                }
                continue;
            }
            if(menu.settings_page==5){
                if(high_scores_back(&event)) {menu.settings_page=0;menu.selected=HIGH_SCORES;pause_panel(&menu);pause_draw(&menu);}
                continue;
            }
            if(menu.settings_page==4){int result=controls_event(&menu,&event);if(event.type==SDL_WINDOWEVENT)result=1;if(result==2){menu.settings_page=2;menu.selected=4;}if(result){pause_panel(&menu);pause_draw(&menu);}continue;}
            if(menu.settings_page==3){
                int result=trophy_event(&menu,&event);
                if(result==2){menu.settings_page=0;menu.selected=ACHIEVEMENTS;menu.trophy_drag=0;}
                if(result){pause_panel(&menu);pause_draw(&menu);}
                continue;
            }
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_EXPOSED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
                redraw = 1;
            if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONUP) {
                int mouse_x = event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x;
                int mouse_y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
                int row = pause_row_at(&menu, g_win.win, mouse_x, mouse_y);
                if (row >= 0) {
                    redraw = menu.selected != row;
                    menu.selected = row;
                    activate = event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT;
                }
            }
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;
                if ((key == SDLK_ESCAPE || hc_matches(HC_PAUSE,key)) && !event.key.repeat) {
                    if (!menu.settings_page) break;
                    menu.selected=menu.settings_page==6?(hotd4_special_menu()?7:8):menu.settings_page==2?(hotd4_special_menu()?8:9):menu.settings_page==3?ACHIEVEMENTS:SETTINGS;
                    menu.settings_page=(menu.settings_page==2 || menu.settings_page==6)?1:0;
                    redraw = 1;
                } else if (key == SDLK_UP || key == SDLK_DOWN) {
                    int rows = menu_row_count(&menu);
                    menu.selected = (menu.selected + (key == SDLK_UP ? rows-1 : 1)) % rows;
                    redraw = 1;
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    delta = key == SDLK_LEFT ? -1 : 1;
                } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && !event.key.repeat) {
                    activate = 1;
                }
            }
            if(menu.settings_page==6){
                if((activate || delta) && menu.selected<2){
                    int off=gameplay_offscreen_reload(),blood=gameplay_blood_splatter();
                    gameplay_options_set(menu.selected==0?!off:off,menu.selected==1?!blood:blood);
                    gameplay_options_save();redraw=1;
                }else if(activate){menu.settings_page=1;menu.selected=hotd4_special_menu()?7:8;redraw=1;}
            } else if (menu.settings_page == 2) {
                if ((activate || delta) && menu.selected < 6) {
                    input_option_cycle(menu.selected,delta);redraw=1;
                } else if(activate && menu.selected==6){menu.settings_page=4;menu.selected=0;menu.binding_capture=-1;menu.binding_message[0]=0;redraw=1;}
                else if(activate && menu.selected==7){action=RESTART;break;}
                else if(activate){menu.settings_page=1;menu.selected=hotd4_special_menu()?8:9;redraw=1;}
            } else if (menu.settings_page) {
                if ((activate || delta) && menu.selected == 0) {
                    pause_set_hud(delta ? delta : 1);
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 1) {
                    settings()->crosshair = !settings()->crosshair;
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 2) {
                    crosshair_style_cycle(delta);
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 3) {
                    crosshair_size_cycle(delta);
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 4) {
                    crosshair_color_cycle(delta);
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 5) {
                    sinden_border_toggle();
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 6) {
                    sinden_border_width_cycle(delta);
                    redraw = 1;
                } else if ((activate || delta) && menu.selected == 7 && !hotd4_special_menu()) {
                    high_scores_toggle();redraw=1;
                } else if(activate && menu.selected==(hotd4_special_menu()?7:8)){
                    menu.settings_page=6;menu.selected=0;redraw=1;
                } else if (activate) {
                    menu.settings_page = 2;
                    menu.selected = 0;
                    redraw = 1;
                }
            } else if (activate) {
                if(hotd4_special_menu() && menu.selected==3){action=MAIN_MENU;break;}
                if (menu.selected == PHOTO_MODE) {
                    menu.photo = menu.photo_guide = 1;
                    menu.filter_dirty = 1;
                    menu.photo_status = 0;
                    menu.camera = (PhotoCamera){.hide_hud = 1};
                    menu.photo_ready = photo_camera_available && photo_camera_available() &&
                                       photo_refresh(&menu, &menu.camera);
                    SDL_ShowCursor(SDL_DISABLE);
                    pause_panel(&menu);
                    pause_draw(&menu);
                    continue;
                }
                if (menu.selected != SETTINGS && menu.selected != ACHIEVEMENTS && menu.selected != HIGH_SCORES) { action = menu.selected; break; }
                menu.settings_page = menu.selected == ACHIEVEMENTS ? 3 : menu.selected == HIGH_SCORES ? 5 : 1;
                menu.selected = 0;
                redraw = 1;
            }
            if (redraw) { pause_panel(&menu); pause_draw(&menu); }
        }
        photo_stop_drag(&menu);
        raw_suspended=0;raw_release();
        host_audio_pause_devices(0);
        host_guest_clock_pause(0);
        fprintf(stderr, "[pause] closed at frame %u (action %d)\n", g_frame, action);
        SDL_SetWindowGrab(g_win.win, grab);
        SDL_SetRelativeMouseMode(relative);
        if (cursor >= 0) SDL_ShowCursor(cursor);
        SDL_FlushEvent(SDL_MOUSEMOTION);
        g_held = g_pad_held = g_cab_latch = 0;
        g_buttons = 0;
        g_in_tail = g_in_head;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
    glDeleteFramebuffers(1, &copy_fbo);
    glDeleteFramebuffers(1, &menu.photo_fbo);
    glDeleteTextures(1, &menu.photo_frame);
    photo_filter_destroy(&menu.filter_pass);
    glDeleteTextures(1, &menu.frame);
    glDeleteTextures(1, &menu.panel);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack);
    glUseProgram((GLuint)program);
    return action;
}
#include "achievement_runtime.h"
#endif


/* The end of a frame: the GL and the headset's part. On the GL thread when
 * there is one. */
static void frame_gl(void *arg)
{
    (void)arg;
#ifdef LINDBERGH_VULKAN
    lvk_host_present();
    if (frame_timing_on) frame_timing_record(g_frame,SDL_GetPerformanceCounter(),SDL_GetPerformanceFrequency());
    maybe_peek();
    ++g_frame;
#else
#if defined(LINDBERGH_OPENXR)
    g_t_game += since_mark();
    stereo_frame_end();                 /* the second eye, then both to the headset */
    g_t_eye2 += since_mark();
    xr_frame_end();
    if (frame_timing_on) frame_timing_record(g_frame,SDL_GetPerformanceCounter(),SDL_GetPerformanceFrequency());
    g_t_submit += since_mark();
#else
    if (stereo_enabled()) {             /* draw the second eye, show both */
        stereo_frame_end();
        if(g_guest_viewport_coords && stereo_wide(NULL,NULL,NULL)) {
            GLint fb;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);glBindFramebuffer(GL_FRAMEBUFFER,0);
            int x=0,y=0,ww=g_win.w,hh=g_win.h;host_viewport_scale(&x,&y,&ww,&hh);
            glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fb);stereo_present_desktop(x,y,ww,hh);
        } else {int w,h;picture_size(&w,&h);stereo_present_desktop(0,0,w,h);}
    }
#endif
    if(!g_guest_viewport_coords)maybe_screenshot(g_frame);
    maybe_peek();
    g_frame++;
#if defined(LINDBERGH_OPENXR)
    xr_frame_begin();
    g_t_wait += since_mark();
    if (0) {
#else
    if (g_win.win) {
#endif
        /* Present the window's own framebuffer whatever the game left bound:
         * it ends a frame with one of its offscreen FBOs current, which the
         * Lindbergh's driver ignored at swap and Apple's does not. */
        GLint fb = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        if (fb) glBindFramebuffer(GL_FRAMEBUFFER, 0);

        int win_w = 0, win_h = 0;
        SDL_GL_GetDrawableSize(g_win.win, &win_w, &win_h);

        /* Clear the letterbox bars without feeding their state back to the game. */
        if (g_vp_x > 0 || g_vp_y > 0) {
            glPushAttrib(GL_SCISSOR_BIT | GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (g_vp_x > 0) {
                glScissor(0, 0, g_vp_x, win_h);
                glClear(GL_COLOR_BUFFER_BIT);
                glScissor(g_vp_x + g_vp_w, 0, win_w - (g_vp_x + g_vp_w), win_h);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (g_vp_y > 0) {
                glScissor(0, 0, win_w, g_vp_y);
                glClear(GL_COLOR_BUFFER_BIT);
                glScissor(0, g_vp_y + g_vp_h, win_w, win_h - (g_vp_y + g_vp_h));
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glPopAttrib();
        }

#ifdef HOTD4_DESKTOP_PAUSE
        if (g_pause_requested) {
            g_pause_requested = 0;
            int action = hotd4_pause_loop(win_w, win_h);
            if (action == RESTART) exit(74);
            if (action == MAIN_MENU) exit(0);
            if (action == RESUME) {
                /* The modal loop presented its own frames. The guest supplies
                 * a fresh back buffer on its next swap. */
                if (fb) glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
                return;
            }
        }
#endif

        /* Draw crosshair overlay on top of frame */
        draw_crosshair_overlay(win_w, win_h);
#ifdef HOTD4_DESKTOP_PAUSE
        if(!hotd4_special_menu())high_scores_hud_draw(win_w,win_h);
        achievement_draw(win_w, win_h);
        sinden_border_draw(win_w, win_h);
#endif

        if(g_guest_viewport_coords)maybe_screenshot(g_frame-1);
        SDL_GL_SwapWindow(g_win.win);
        if (fb) glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    }
#if !defined(LINDBERGH_OPENXR)
    if (frame_timing_on) frame_timing_record(g_frame-1,SDL_GetPerformanceCounter(),SDL_GetPerformanceFrequency());
#endif
    stereo_frame_begin();
#endif
}

/* The game thread's end of a frame. */
static void host_frame_done(void)
{
#ifdef LINDBERGH_VULKAN
    lvk_host_input();
#endif
    {
        static int after = -1;
        if (after < 0) after = env_int("LINDBERGH_EXIT_AFTER", 0);
        if (after && g_frame >= (unsigned)after) {
            glq_drain();
            fprintf(stderr, "[window] LINDBERGH_EXIT_AFTER: %u frames done\n", g_frame);
            exit(0);
        }
    }
#ifdef LINDBERGH_VULKAN
    {   /* the game thread's CPU time per frame, smoothed, for the stats panel */
        static double last, avg = -1;
        struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        double cpu = ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
        if (last > 0) { double d = cpu - last; avg = avg < 0 ? d : avg * 0.9 + d * 0.1; lg_stats_set_game_ms((float)avg); }
        last = cpu;
    }
#endif
    {   /* LINDBERGH_LATENCY: frames that took far longer than 1/60 s */
        static Uint64 prev; Uint64 now = SDL_GetPerformanceCounter();
        double ms = prev ? (now - prev) * 1000.0 / SDL_GetPerformanceFrequency() : 0; prev = now;
        if (ms > 40 && env_int("LINDBERGH_LATENCY", 0)) fprintf(stderr, "[lat] frame %u took %.0f ms\n", g_frame, ms);
        /* pacing, for the 5-second report: frames late for 60 Hz, and the worst */
        if (ms > 17.5) g_late17++;
        if (ms > 25.0) g_late25++;
        if (ms > g_worst) g_worst = ms;
    }
    {   /* frame rate, every 5 seconds */
        static Uint32 t0; static unsigned f0;
        Uint32 now = SDL_GetTicks();
        if (!t0) { t0 = now; f0 = g_frame; }
        else if (now - t0 >= 5000) {
#if defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
            unsigned n = g_frame - f0 ? g_frame - f0 : 1;
            fprintf(stderr, "[perf] frame %u, %.1f fps; ms per frame: game %.1f, second eye %.1f, submit %.1f, headset wait %.1f;"
                            " late %u (over 25 ms %u), worst %.0f ms\n",
                    g_frame, (g_frame - f0) * 1000.0 / (now - t0),
                    g_t_game / n, g_t_eye2 / n, g_t_submit / n, g_t_wait / n, g_late17, g_late25, g_worst);
            g_late17 = g_late25 = 0; g_worst = 0;
            g_t_game = g_t_eye2 = g_t_submit = g_t_wait = 0;
#ifdef LINDBERGH_GL4ES
            {   /* shaders built in those 5 seconds: the driver's compiles and links
                 * (gl4es), and the programs the disk caches gave or did not */
                extern unsigned gl4es_stat_compiles, gl4es_stat_links, gl4es_stat_fpe_hits, gl4es_stat_fpe_built, gl4es_stat_app_hits, gl4es_stat_app_built;
                extern double gl4es_stat_compile_ms, gl4es_stat_link_ms;
                void arbprog_build_stats(unsigned *, unsigned *, double *);
                unsigned ah, ab; double ams;
                arbprog_build_stats(&ah, &ab, &ams);
                if (gl4es_stat_compiles || gl4es_stat_links || ah || ab || gl4es_stat_fpe_hits || gl4es_stat_fpe_built || gl4es_stat_app_hits || gl4es_stat_app_built)
                    fprintf(stderr, "[shaders] frame %u: driver compiled %u (%.0f ms), linked %u (%.0f ms); Cg programs %u cached, %u built (%.0f ms);"
                                    " fixed-function %u cached, %u built; all programs %u cached, %u built\n", g_frame, gl4es_stat_compiles, gl4es_stat_compile_ms,
                            gl4es_stat_links, gl4es_stat_link_ms, ah, ab, ams, gl4es_stat_fpe_hits, gl4es_stat_fpe_built,
                            gl4es_stat_app_hits, gl4es_stat_app_built);
                gl4es_stat_compiles = gl4es_stat_links = gl4es_stat_fpe_hits = gl4es_stat_fpe_built = 0;
                gl4es_stat_app_hits = gl4es_stat_app_built = 0;
                gl4es_stat_compile_ms = gl4es_stat_link_ms = 0;
            }
#endif
#else
            /* the game thread's own CPU time: what a slower CPU (the Quest's)
             * scales, where the frame rate here is held at the display's */
            static double cpu0;
            struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
            double cpu = ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
            fprintf(stderr, "[perf] frame %u, %.1f fps; game thread CPU %.2f ms per frame\n", g_frame,
                    (g_frame - f0) * 1000.0 / (now - t0), cpu0 ? (cpu - cpu0) / (g_frame - f0 ? g_frame - f0 : 1) : 0.0);
            cpu0 = cpu;
#endif
            glq_report(g_frame - f0);
            t0 = now; f0 = g_frame;
        }
    }
#if defined(__APPLE__)
    if (g_frame % 60u == 0) host_menu_install();
#endif
    pump();
}

/* This game opens its window through GLUT; the X11/GLX seam that a GLX title
 * needs lives in hle_window.c and is Windows-only so far. */
void hle_register_window(void) { }
