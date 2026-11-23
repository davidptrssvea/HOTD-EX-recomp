#include "host_settings.h"
#include "lindbergh_rt.h"

static void apply_anisotropy(GLenum target)
{
    HostSettings *st = settings();
    if (st && st->af > 1) {
        static float max_aniso = -1.0f;
        if (max_aniso < 0.0f) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
            if (max_aniso < 1.0f) max_aniso = 1.0f;
            fprintf(stderr, "[gl] AF active: max %.1fx, requested %dx\n", max_aniso, st->af);
        }
        float val = (float)st->af;
        if (val > max_aniso) val = max_aniso;
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, val);
    }
}

