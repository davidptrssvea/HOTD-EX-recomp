/*
 * host_settings.c - the player's settings, and lindbergh.ini.
 *
 * The file is plain key = value lines. Unknown keys are ignored and missing
 * ones keep their defaults, so an old file keeps working. Environment
 * variables still win over the file (LINDBERGH_MUTE, LINDBERGH_COUNTRY,
 * LINDBERGH_GUN_CAL) - they are how unattended test runs are configured.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "host_settings.h"

#define INI "lindbergh.ini"

/* A standalone launcher can keep settings outside the original arcade dump.
 * Unset keeps the established working-directory behavior for other ports. */
static const char *settings_file(void)
{
    const char *path=getenv("LINDBERGH_SETTINGS_FILE");
    return path && *path ? path : INI;
}


static HostSettings g_set;
static int g_loaded;

static const struct { const char *key, *name; int dflt; } ACTIONS[ACT_COUNT] = {
    [ACT_COIN1]   = { "coin1",   "Coin (Player 1)",   '5' },
    [ACT_START1]  = { "start1",  "Start (Player 1)",  '1' },
    [ACT_TRIGGER] = { "trigger", "Trigger",           MOUSE_BIND(SDL_BUTTON_LEFT) },
    [ACT_RELOAD]  = { "reload",  "Reload",            MOUSE_BIND(SDL_BUTTON_RIGHT) },
    [ACT_ACTION]  = { "action",  "Action button",     ' ' },
    [ACT_FIREMODE] = { "firemode", "Fire mode / grenade launcher", 'f' },
    [ACT_COIN2]   = { "coin2",   "Coin (Player 2)",   '6' },
    [ACT_START2]  = { "start2",  "Start (Player 2)",  '2' },
    [ACT_TEST]    = { "test",    "Test (operator)",   't' },
    [ACT_SERVICE] = { "service", "Service (operator)", 's' },
    [ACT_MUTE]    = { "mute",    "Mute",              'm' },
    /* a lever cabinet's (Virtua Tennis 3): the stick and three buttons */
    [ACT_UP]      = { "up",      "Lever up",          SDLK_UP },
    [ACT_DOWN]    = { "down",    "Lever down",        SDLK_DOWN },
    [ACT_LEFT]    = { "left",    "Lever left",        SDLK_LEFT },
    [ACT_RIGHT]   = { "right",   "Lever right",       SDLK_RIGHT },
    [ACT_BUTTON1] = { "button1", "Button 1 (topspin)", 'z' },
    [ACT_BUTTON2] = { "button2", "Button 2 (lob)",    'x' },
    [ACT_BUTTON3] = { "button3", "Button 3 (slice)",  'c' },
};

const char *settings_action_name(HostAction a) { return ACTIONS[a].name; }

void settings_reset_controls(void)
{
    for (int a = 0; a < ACT_COUNT; a++) g_set.bind[a] = ACTIONS[a].dflt;
    g_set.leave_reload = 1;
}

/* The game maps the gun through its own calibration, which stretches the raw
 * range: measured, the sight lands at 1.14x + 21, 1.06y + 17. The host sends
 * the inverse so the sight sits under the pointer. */
static double g_aim_default[4] = { 1.139, 20.6, 1.056, 16.7 };

/* A game whose gun needs no correction (or another one) says so before the
 * settings load; "Reset Aim" returns to it too. */
void host_aim_default(double sx, double ox, double sy, double oy)
{
    g_aim_default[0] = sx; g_aim_default[1] = ox;
    g_aim_default[2] = sy; g_aim_default[3] = oy;
}

void settings_reset_aim(void)
{
    g_set.aim_sx = g_aim_default[0]; g_set.aim_ox = g_aim_default[1];
    g_set.aim_sy = g_aim_default[2]; g_set.aim_oy = g_aim_default[3];
}

void settings_binding_name(int b, char *out, int n)
{
    if (!b) { snprintf(out, n, "none"); return; }
    if (IS_MOUSE_BIND(b)) {
        int m = MOUSE_OF(b);
        snprintf(out, n, "%s", m == SDL_BUTTON_LEFT ? "Left Click" : m == SDL_BUTTON_RIGHT ? "Right Click"
                               : m == SDL_BUTTON_MIDDLE ? "Middle Click" : "Mouse Button");
        if (m > SDL_BUTTON_RIGHT) snprintf(out, n, "Mouse Button %d", m);
        return;
    }
    const char *k = SDL_GetKeyName((SDL_Keycode)b);
    snprintf(out, n, "%s", k && *k ? k : "?");
}

static int parse_bind(const char *v)
{
    if (!strncmp(v, "mouse", 5)) return MOUSE_BIND(atoi(v + 5));
    if (!strcmp(v, "none")) return 0;
    if (v[0] && !v[1]) return (unsigned char)v[0];
    if (!strcasecmp(v, "space")) return ' ';
    if (!strcasecmp(v, "enter") || !strcasecmp(v, "return")) return SDLK_RETURN;
    return atoi(v);
}

static void load(void)
{
    settings_reset_controls();
    settings_reset_aim();
    g_set.muted = 0;                 /* sound on by default */
    g_set.volume = 100;
    g_set.country = 1;
    g_set.window_w = 1920;
    g_set.window_h = 1080;
    g_set.fullscreen = 0;
    g_set.aa = 4;
    g_set.af = 16;
    g_set.raw_mouse = 1;
    g_set.mouse_sens = 1.0f;
    g_set.mouse_grab = 1;
    g_set.crosshair = 1;

    FILE *f = fopen(settings_file(), "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char k[64], v[128];
            if (sscanf(line, " %63[^= ] = %127[^\n]", k, v) != 2 || k[0] == '#') continue;
            int done = 0;
            for (int a = 0; a < ACT_COUNT && !done; a++)
                if (!strcmp(k, ACTIONS[a].key)) { g_set.bind[a] = parse_bind(v); done = 1; }
            if (done) continue;
            if (!strcmp(k, "aim")) sscanf(v, "%lf,%lf,%lf,%lf", &g_set.aim_sx, &g_set.aim_ox, &g_set.aim_sy, &g_set.aim_oy);
            else if (!strcmp(k, "leave_reload")) g_set.leave_reload = atoi(v);
            else if (!strcmp(k, "muted"))        g_set.muted = atoi(v);
            else if (!strcmp(k, "volume"))       g_set.volume = atoi(v);
            else if (!strcmp(k, "country"))      g_set.country = atoi(v);
            else if (!strcmp(k, "width") || !strcmp(k, "window_w")) g_set.window_w = atoi(v);
            else if (!strcmp(k, "height") || !strcmp(k, "window_h")) g_set.window_h = atoi(v);
            else if (!strcmp(k, "resolution") || !strcmp(k, "window")) {
                int rw = 0, rh = 0;
                if (sscanf(v, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0) {
                    g_set.window_w = rw;
                    g_set.window_h = rh;
                }
            }
            else if (!strcmp(k, "fullscreen"))   g_set.fullscreen = atoi(v);
            else if (!strcmp(k, "aa") || !strcmp(k, "msaa")) g_set.aa = atoi(v);
            else if (!strcmp(k, "af") || !strcmp(k, "anisotropy")) g_set.af = atoi(v);
            else if (!strcmp(k, "raw_mouse") || !strcmp(k, "raw_input")) g_set.raw_mouse = atoi(v);
            else if (!strcmp(k, "mouse_sens") || !strcmp(k, "sensitivity")) g_set.mouse_sens = (float)atof(v);
            else if (!strcmp(k, "mouse_grab") || !strcmp(k, "grab")) g_set.mouse_grab = atoi(v);
            else if (!strcmp(k, "crosshair") || !strcmp(k, "reticle")) g_set.crosshair = atoi(v);
        }
        fclose(f);
        fprintf(stderr, "[settings] loaded %s\n", settings_file());
    }
    const char *v;
    if ((v = getenv("LINDBERGH_MUTE")) && *v)    g_set.muted = *v != '0';
    if ((v = getenv("LINDBERGH_COUNTRY")) && *v) g_set.country = atoi(v);
    if ((v = getenv("LINDBERGH_WINDOW")) && *v) {
        int rw = 0, rh = 0;
        if (sscanf(v, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0) {
            g_set.window_w = rw;
            g_set.window_h = rh;
        }
    }
    if ((v = getenv("LINDBERGH_FULLSCREEN")) && *v) g_set.fullscreen = atoi(v);
    if ((v = getenv("LINDBERGH_AA")) && *v)         g_set.aa = atoi(v);
    if ((v = getenv("LINDBERGH_AF")) && *v)         g_set.af = atoi(v);
    if ((v = getenv("LINDBERGH_RAW_MOUSE")) && *v)  g_set.raw_mouse = atoi(v);
    if ((v = getenv("LINDBERGH_MOUSE_SENS")) && *v) g_set.mouse_sens = (float)atof(v);
    if ((v = getenv("LINDBERGH_MOUSE_GRAB")) && *v) g_set.mouse_grab = atoi(v);
    if ((v = getenv("LINDBERGH_CROSSHAIR")) && *v)  g_set.crosshair = atoi(v);
    if ((v = getenv("LINDBERGH_GUN_CAL")) && *v)
        sscanf(v, "%lf,%lf,%lf,%lf", &g_set.aim_sx, &g_set.aim_ox, &g_set.aim_sy, &g_set.aim_oy);
    if (g_set.volume < 0) g_set.volume = 0;
    if (g_set.volume > 100) g_set.volume = 100;
}

HostSettings *settings(void)
{
    if (!g_loaded) { g_loaded = 1; load(); }
    return &g_set;
}

void settings_save(void)
{
    /* unattended runs are configured by the environment; never write theirs */
    if (getenv("LINDBERGH_EXIT_AFTER")) return;
    FILE *f = fopen(settings_file(), "w");
    if (!f) { fprintf(stderr, "[settings] cannot write %s\n", settings_file()); return; }
    fprintf(f, "# Ghost Squad Evolution (lindberghrecomp) settings - edited from the menus.\n"
               "# Bindings are SDL keycodes, mouseN for mouse button N, or none.\n");
    for (int a = 0; a < ACT_COUNT; a++) {
        int b = g_set.bind[a];
        if (IS_MOUSE_BIND(b)) fprintf(f, "%s = mouse%d\n", ACTIONS[a].key, MOUSE_OF(b));
        else if (!b)          fprintf(f, "%s = none\n", ACTIONS[a].key);
        else                  fprintf(f, "%s = %d\n", ACTIONS[a].key, b);
    }
    fprintf(f, "aim = %.4f,%.3f,%.4f,%.3f\n", g_set.aim_sx, g_set.aim_ox, g_set.aim_sy, g_set.aim_oy);
    fprintf(f, "leave_reload = %d\nmuted = %d\nvolume = %d\ncountry = %d\n",
            g_set.leave_reload, g_set.muted, g_set.volume, g_set.country);
    fprintf(f, "\n# Display & Graphics\n");
    fprintf(f, "width = %d\nheight = %d\nfullscreen = %d\naa = %d\naf = %d\n",
            g_set.window_w, g_set.window_h, g_set.fullscreen, g_set.aa, g_set.af);
    fprintf(f, "\n# Mouse Controls\n");
    fprintf(f, "raw_mouse = %d\nmouse_sens = %.2f\nmouse_grab = %d\ncrosshair = %d\n",
            g_set.raw_mouse, (double)g_set.mouse_sens, g_set.mouse_grab, g_set.crosshair);
    fclose(f);
}
