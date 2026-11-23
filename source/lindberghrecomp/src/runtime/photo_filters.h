#ifndef HOTD4_PHOTO_FILTERS_H
#define HOTD4_PHOTO_FILTERS_H

typedef enum {
    PHOTO_FILTER_ORIGINAL,
    PHOTO_FILTER_MONOCHROME,
    PHOTO_FILTER_NOIR,
    PHOTO_FILTER_SATURATED,
    PHOTO_FILTER_COUNT
} PhotoFilter;

static const wchar_t *const photo_filter_names[] = {
    L"Original", L"Black & White", L"Film Noir", L"High Saturation"
};

typedef struct {
    GLuint program, texture, framebuffer;
    GLint mode, size;
} PhotoFilterPass;

static const char photo_filter_vertex[] =
    "#version 120\n"
    "void main() { gl_Position = gl_Vertex; gl_TexCoord[0] = gl_MultiTexCoord0; }\n";

static const char photo_filter_fragment[] =
    "#version 120\n"
    "uniform sampler2D picture;\n"
    "uniform int mode;\n"
    "uniform vec2 size;\n"
    "float noise(vec2 p) {\n"
    "    return fract(sin(dot(mod(p, 4096.0), vec2(12.9898, 78.233))) * 43758.5453);\n"
    "}\n"
    "float filmScratches(vec2 uv) {\n"
    "    float lane = floor(uv.x * 13.0);\n"
    "    float seed = noise(vec2(lane, 57.0));\n"
    "    float center = 0.2 + 0.58 * seed;\n"
    "    float start = 0.63 * noise(vec2(lane, 19.0)) - 0.18;\n"
    "    float end = start + 0.24 + 0.69 * noise(vec2(lane, 34.0));\n"
    "    float x = fract(uv.x * 13.0) - center + (seed - 0.5) * (uv.y - 0.5) * 0.035;\n"
    "    float width = 0.005 + 0.009 * noise(vec2(lane, 89.0));\n"
    "    float segment = smoothstep(start, start + 0.02, uv.y) * (1.0 - smoothstep(end - 0.025, end, uv.y));\n"
    "    float erosion = 0.48 + 0.52 * noise(vec2(lane, floor(uv.y * 151.0)));\n"
    "    return (1.0 - smoothstep(0.12 * width, width, abs(x))) * segment * erosion * step(0.26, seed);\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].xy;\n"
    "    vec3 color = texture2D(picture, uv).rgb;\n"
    "    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "    if (mode == 1) {\n"
    "        color = vec3(luma);\n"
    "    } else if (mode == 2) {\n"
    "        vec2 edge = (uv - 0.5) * vec2(1.16, 1.0);\n"
    "        float vignette = 1.0 - 0.45 * smoothstep(0.15, 0.53, dot(edge, edge));\n"
    "        float contrast = smoothstep(0.075, 0.91, luma);\n"
    "        float grain = (noise(floor(uv * size * 0.77)) - 0.5) * 0.105;\n"
    "        grain += (noise(floor(uv * size * 0.29) + vec2(19.0, 47.0)) - 0.5) * 0.055;\n"
    "        float scratch = filmScratches(uv) * 0.28;\n"
    "        color = vec3(0.021 + contrast * vignette * 0.93 + grain * (0.62 + 0.38 * (1.0 - contrast)) + scratch);\n"
    "    } else {\n"
    "        color = mix(vec3(luma), color, 1.75);\n"
    "    }\n"
    "    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
    "}\n";

static GLuint photo_compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled) return shader;
    char log[512] = {0};
    glGetShaderInfoLog(shader, sizeof log, NULL, log);
    fprintf(stderr, "[photo] filter shader failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

static GLuint photo_filter_program(void)
{
    GLuint vertex = photo_compile_shader(GL_VERTEX_SHADER, photo_filter_vertex);
    GLuint fragment = photo_compile_shader(GL_FRAGMENT_SHADER, photo_filter_fragment);
    GLuint program = vertex && fragment ? glCreateProgram() : 0;
    if (program) {
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[512] = {0};
            glGetProgramInfoLog(program, sizeof log, NULL, log);
            fprintf(stderr, "[photo] filter program failed: %s\n", log);
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    return program;
}

static void photo_filter_destroy(PhotoFilterPass *pass)
{
    if (pass->program) glDeleteProgram(pass->program);
    glDeleteFramebuffers(1, &pass->framebuffer);
    glDeleteTextures(1, &pass->texture);
}

#endif
