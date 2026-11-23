#pragma once
#include <stdint.h>
#include <string.h>

/* Raw relative motion, before cursor clamping or aim sensitivity. A shake
 * requires two substantial reversals; a straight sweep or jitter is not one. */
typedef struct {
    int direction[2], distance[2], opposite[2];
    unsigned reversals[2];
    uint32_t turn_time[2], pulse_time, pulse_until, pulses;
} MouseShake;

static inline void mouse_shake_reset(MouseShake *s) { memset(s, 0, sizeof *s); }

static inline void mouse_shake_motion(MouseShake *s, int dx, int dy, uint32_t now)
{
    const int motion[2] = { dx, dy };
    for (int axis = 0; axis < 2; ++axis) {
        int delta = motion[axis];
        if (!delta) continue;
        int direction = delta > 0 ? 1 : -1;
        int amount = delta > 0 ? (delta > 4096 ? 4096 : delta) : (delta < -4096 ? 4096 : -delta);
        if (!s->direction[axis]) s->direction[axis] = direction;
        if (direction == s->direction[axis]) {
            s->distance[axis] += amount;
            if (s->distance[axis] > 4096) s->distance[axis] = 4096;
            s->opposite[axis] = 0;
        } else {
            s->opposite[axis] += amount;
            if (s->opposite[axis] > 4096) s->opposite[axis] = 4096;
            if (s->opposite[axis] < 18 || s->distance[axis] < 18) continue;
            s->direction[axis] = direction;
            s->distance[axis] = s->opposite[axis];
            s->opposite[axis] = 0;
            s->reversals[axis] = s->reversals[axis] && now - s->turn_time[axis] <= 350 ? s->reversals[axis] + 1 : 1;
            s->turn_time[axis] = now;
            if (s->reversals[axis] >= 2 && (!s->pulses || now - s->pulse_time >= 90)) {
                s->pulse_time = now;
                s->pulse_until = now + 40;
                ++s->pulses;
            }
        }
    }
}

static inline int mouse_shake_pulse(const MouseShake *s, uint32_t now)
{
    return s->pulses && (int32_t)(s->pulse_until - now) > 0;
}
