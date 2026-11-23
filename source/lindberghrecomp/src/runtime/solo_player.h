#ifndef HOTD4_SOLO_PLAYER_H
#define HOTD4_SOLO_PLAYER_H
#include "cabinet_buttons.h"
/* Swap logical player seats; physical devices and held/latch state stay put. */
static inline unsigned solo_player_buttons(unsigned buttons,int kate)
{
    if(!kate)return buttons;
    const unsigned first=CAB_P1_TRIGGER|CAB_P1_RELOAD|CAB_P1_ACTION|CAB_P1_FIREMODE;
    const unsigned second=first<<CAB_P2_SHIFT;
    return (buttons&~(first|second|CAB_P1_START|CAB_P2_START)) |
           ((buttons&first)<<CAB_P2_SHIFT) | ((buttons&second)>>CAB_P2_SHIFT) |
           (buttons&CAB_P1_START?CAB_P2_START:0) | (buttons&CAB_P2_START?CAB_P1_START:0);
}
static inline int solo_player_enabled(int supported,int selected,int mode)
{
    return supported && selected==1 && mode>=0 && mode<3;
}
#endif
