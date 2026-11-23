#ifndef HOTD4_SINDEN_BORDER_H
#define HOTD4_SINDEN_BORDER_H

static int sinden_enabled = -1;
static char sinden_profile[2048];

static int sinden_border_enabled(void)
{
    if (sinden_enabled >= 0) return sinden_enabled;
    const char *profile = getenv("HOTD4_SINDEN_PROFILE");
    if (profile && *profile) snprintf(sinden_profile, sizeof sinden_profile, "%s", profile);
    else if (getenv("HOTD4_SETTINGS_DIR") && *getenv("HOTD4_SETTINGS_DIR"))
        snprintf(sinden_profile,sizeof sinden_profile,"%s/sinden-profile.txt",getenv("HOTD4_SETTINGS_DIR"));
    else {
        GetModuleFileNameA(NULL, sinden_profile, sizeof sinden_profile);
        char *slash = strrchr(sinden_profile, '\\');
        if (slash) strcpy(slash + 1, "sinden-profile.txt");
        else strcpy(sinden_profile, "sinden-profile.txt");
    }
    sinden_enabled = 0;
    FILE *file = fopen(sinden_profile, "rb");
    if (file) { sinden_enabled = fgetc(file) == '1'; fclose(file); }
    return sinden_enabled;
}

static void sinden_border_toggle(void)
{
    sinden_enabled = !sinden_border_enabled();
    FILE *file = fopen(sinden_profile, "wb");
    if (file) { fputs(sinden_enabled ? "1\n" : "0\n", file); fclose(file); }
    else fprintf(stderr, "[sinden] Could not save %s\n", sinden_profile);
}

/* Percentage of the original 3%-high border. Default 50 halves both edges. */
static int sinden_width;
static char sinden_width_profile[2048];
static const int sinden_width_values[4]={25,50,75,100};
static const wchar_t *sinden_width_names[4]={L"Extra Thin (0.75%)",L"Thin (1.5%)",L"Medium (2.25%)",L"Original (3%)"};
static int sinden_border_width(void)
{
    if(sinden_width)return sinden_width;
    sinden_border_enabled();
    const char *custom=getenv("HOTD4_SINDEN_WIDTH_PROFILE");
    if(custom && *custom)snprintf(sinden_width_profile,sizeof sinden_width_profile,"%s",custom);
    else {
        snprintf(sinden_width_profile,sizeof sinden_width_profile,"%s",sinden_profile);
        char *a=strrchr(sinden_width_profile,'/'),*b=strrchr(sinden_width_profile,'\\');
        char *slash=a && (!b || a>b)?a:b;
        if(slash)snprintf(slash+1,sizeof sinden_width_profile-(slash+1-sinden_width_profile),"sinden-width.txt");
        else snprintf(sinden_width_profile,sizeof sinden_width_profile,"sinden-width.txt");
    }
    sinden_width=50;FILE*f=fopen(sinden_width_profile,"rb");
    if(f){int value=0;if(fscanf(f,"%d",&value)==1)for(int i=0;i<4;i++)if(value==sinden_width_values[i])sinden_width=value;fclose(f);}
    return sinden_width;
}
static int sinden_border_width_index(void)
{
    int value=sinden_border_width();for(int i=0;i<4;i++)if(value==sinden_width_values[i])return i;return 1;
}
static void sinden_border_width_cycle(int delta)
{
    int i=(sinden_border_width_index()+(delta<0?3:1))%4;sinden_width=sinden_width_values[i];
    FILE*f=fopen(sinden_width_profile,"wb");if(f){fprintf(f,"%d",sinden_width);fclose(f);}else fprintf(stderr,"[sinden] Could not save border width\n");
}
static void sinden_border_pixels(int height,int *white,int *black)
{
    float scale=sinden_border_width()/100.0f;
    *white=(int)fmaxf(ceilf(height*.03f*scale),ceilf(8*scale));
    *black=(int)fmaxf(ceilf(height*.005f*scale),ceilf(2*scale));
}
static void sinden_border_draw(int width, int height)
{
    if (!sinden_border_enabled() || width <= 0 || height <= 0) return;
    int white,black; sinden_border_pixels(height,&white,&black);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    for (int pass = 0; pass < 2; ++pass) {
        int edge = pass ? white : white + black;
        float color = pass ? 1.0f : 0.0f;
        glClearColor(color, color, color, 1.0f);
        glScissor(0, 0, width, edge); glClear(GL_COLOR_BUFFER_BIT);
        glScissor(0, height-edge, width, edge); glClear(GL_COLOR_BUFFER_BIT);
        glScissor(0, 0, edge, height); glClear(GL_COLOR_BUFFER_BIT);
        glScissor(width-edge, 0, edge, height); glClear(GL_COLOR_BUFFER_BIT);
    }
    glPopAttrib();
}
static int crosshair_style = -1;
static char crosshair_profile[2048];
static int crosshair_style_get(void)
{
    if (crosshair_style >= 0) return crosshair_style;
    const char *path = getenv("HOTD4_CROSSHAIR_PROFILE");
    if (path && *path) snprintf(crosshair_profile, sizeof crosshair_profile, "%s", path);
    else {
        GetModuleFileNameA(NULL, crosshair_profile, sizeof crosshair_profile);
        char *slash = strrchr(crosshair_profile, '\\');
        if (slash) strcpy(slash+1, "crosshair-profile.txt");
        else strcpy(crosshair_profile, "crosshair-profile.txt");
    }
    crosshair_style = 0;
    FILE *file = fopen(crosshair_profile, "rb");
    if (file) { int value=fgetc(file)-'0'; if(value>=0 && value<=3)crosshair_style=value; fclose(file); }
    return crosshair_style;
}
static void crosshair_style_cycle(int delta)
{
    crosshair_style=(crosshair_style_get()+(delta<0?3:1))%4;
    FILE *file=fopen(crosshair_profile,"wb");
    if(file){fprintf(file,"%d",crosshair_style);fclose(file);}
    else fprintf(stderr,"[crosshair] Could not save %s\n",crosshair_profile);
}
static int crosshair_size = 0;
static char crosshair_size_profile[2048];
static int crosshair_size_get(void)
{
    if(crosshair_size)return crosshair_size;
    const char *path=getenv("HOTD4_CROSSHAIR_SIZE_PROFILE");
    if(path && *path)snprintf(crosshair_size_profile,sizeof crosshair_size_profile,"%s",path);
    else {
        GetModuleFileNameA(NULL,crosshair_size_profile,sizeof crosshair_size_profile);
        char *slash=strrchr(crosshair_size_profile,'\\');
        if(slash)strcpy(slash+1,"crosshair-size.txt");
        else strcpy(crosshair_size_profile,"crosshair-size.txt");
    }
    crosshair_size=100;
    FILE *file=fopen(crosshair_size_profile,"rb");
    if(file){int value=0;if(fscanf(file,"%d",&value)==1 && value>=50 && value<=300 && value%25==0)crosshair_size=value;fclose(file);}
    return crosshair_size;
}
static void crosshair_size_cycle(int delta)
{
    int value=crosshair_size_get()+(delta<0?-25:25);
    crosshair_size=value<50?200:value>200?50:value;
    FILE *file=fopen(crosshair_size_profile,"wb");
    if(file){fprintf(file,"%d",crosshair_size);fclose(file);}
    else fprintf(stderr,"[crosshair] Could not save size\n");
}
static const wchar_t *crosshair_color_names[] = {L"Original",L"Red",L"Blue",L"Green",L"Yellow",L"White",L"Cyan",L"Magenta",L"Orange"};
static int crosshair_color = -1;
static char crosshair_color_profile[2048];
static int crosshair_color_get(void)
{
    if(crosshair_color>=0)return crosshair_color;
    const char *path=getenv("HOTD4_CROSSHAIR_COLOR_PROFILE");
    if(path && *path)snprintf(crosshair_color_profile,sizeof crosshair_color_profile,"%s",path);
    else {
        GetModuleFileNameA(NULL,crosshair_color_profile,sizeof crosshair_color_profile);
        char *slash=strrchr(crosshair_color_profile,'\\');
        if(slash)strcpy(slash+1,"crosshair-color.txt");
        else strcpy(crosshair_color_profile,"crosshair-color.txt");
    }
    crosshair_color=0;
    FILE *file=fopen(crosshair_color_profile,"rb");
    if(file){int value=fgetc(file)-'0';if(value>=0 && value<9)crosshair_color=value;fclose(file);}
    return crosshair_color;
}
static void crosshair_color_cycle(int delta)
{
    crosshair_color=(crosshair_color_get()+(delta<0?8:1))%9;
    FILE *file=fopen(crosshair_color_profile,"wb");
    if(file){fprintf(file,"%d",crosshair_color);fclose(file);}
    else fprintf(stderr,"[crosshair] Could not save color\n");
}
static void crosshair_color_apply(GLubyte r,GLubyte g,GLubyte b,GLubyte a)
{
    static const GLubyte colors[8][3]={{249,38,51},{45,161,255},{64,255,50},{255,225,45},{255,255,255},{40,255,255},{255,60,220},{255,140,30}};
    int color=crosshair_color_get();
    if(crosshair_drawing_player) color=(color==2 || (!color && b>r && b>g))?1:2;
    if(color){r=colors[color-1][0];g=colors[color-1][1];b=colors[color-1][2];}
    glColor4ub(r,g,b,a);
}

#include "native_crosshair.h"
static int extra_crosshair_draw(float x,float y,float scale)
{
    int style=crosshair_style_get();
    if(!style)return 0;
    if(style==3)return native_sight_draw(x,y,scale,crosshair_color_get());
    if(style==1){
        float radius=22*scale;
        for(int pass=0;pass<2;pass++){
            float width=(pass?1.5f:3.8f)*scale;
            crosshair_color_apply(45,161,255,pass?255:65);
            glBegin(GL_QUAD_STRIP);
            for(int i=0;i<=80;i++){
                float angle=i*6.2831853f/80;
                glVertex2f(x+cosf(angle)*(radius-width),y+sinf(angle)*(radius-width));
                glVertex2f(x+cosf(angle)*(radius+width),y+sinf(angle)*(radius+width));
            }
            glEnd();
        }
        crosshair_color_apply(70,177,255,255);
        float stroke=1.3f*scale;
        glRectf(x-radius,y-stroke,x+radius,y+stroke);
        glRectf(x-stroke,y-radius,x+stroke,y+radius);
        for(int i=0;i<4;i++){
            float a=i*1.5707963f,dx=cosf(a),dy=sinf(a);
            float tip=29*scale,base=40*scale,wing=4.5f*scale;
            glBegin(GL_TRIANGLES);
            glVertex2f(x+dx*tip,y+dy*tip);
            glVertex2f(x+dx*base-dy*wing,y+dy*base+dx*wing);
            glVertex2f(x+dx*base+dy*wing,y+dy*base-dx*wing);
            glEnd();
        }
    }else{
        float r=18*scale,s=fmaxf(.55f,.65f*scale),arm=25*scale;
        crosshair_color_apply(244,35,48,255);
        glRectf(x-r-s,y-r-s,x+r+s,y-r+s);
        glRectf(x-r-s,y+r-s,x+r+s,y+r+s);
        glRectf(x-r-s,y-r,x-r+s,y+r);
        glRectf(x+r-s,y-r,x+r+s,y+r);
        glRectf(x-arm,y-s,x+arm,y+s);
        glRectf(x-s,y-arm,x+s,y+arm);
    }
    return 1;
}
#endif
