#pragma once
#include "cabinet_buttons.h"
void gameplay_options_load(void);
int gameplay_free_play(void);
int gameplay_mute_credit_sounds(void);
void gameplay_options_set(int offscreen, int blood);
void gameplay_options_save(void);
int gameplay_offscreen_reload(void);
int gameplay_blood_splatter(void);
static inline unsigned gameplay_offscreen_buttons(unsigned buttons, int enabled,
    int x, int y, int p2, int x2, int y2, int w, int h)
{
    if (w <= 0 || h <= 0) return buttons;
    /* Suppress an off-screen shot in both modes: the original arcade code
     * can itself reload on an off-screen trigger, bypassing an Off choice. */
    if ((x < 0 || y < 0 || x >= w || y >= h) && (buttons & CAB_P1_TRIGGER))
        buttons = (buttons & ~CAB_P1_TRIGGER) | (enabled ? CAB_P1_RELOAD : 0);
    if (p2 && (x2 < 0 || y2 < 0 || x2 >= w || y2 >= h) && (buttons & CAB_P2_TRIGGER))
        buttons = (buttons & ~CAB_P2_TRIGGER) | (enabled ? CAB_P2_RELOAD : 0);
    return buttons;
}
