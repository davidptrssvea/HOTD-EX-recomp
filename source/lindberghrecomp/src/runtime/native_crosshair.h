#ifndef HOTD4_NATIVE_CROSSHAIR_H
#define HOTD4_NATIVE_CROSSHAIR_H
/* HC01 is generated locally from the owner's arcade dump by the launcher. */
typedef struct { float xy[8],uv[4],rgba[4],add; } NativeSightSprite;
typedef struct { unsigned width,height,idle,shot; unsigned char *pixels; NativeSightSprite *frames; GLuint textures[9]; } NativeSightPlayer;
static NativeSightPlayer native_sight[2];
static int native_sight_loaded;
static int native_sight_load(void)
{
    if(native_sight_loaded)return native_sight_loaded>0;
    native_sight_loaded=-1;
    char path[2048];const char *dir=getenv("HOTD4_SETTINGS_DIR");
    if(dir && *dir)snprintf(path,sizeof path,"%s/native-crosshair.bin",dir);
    else {GetModuleFileNameA(NULL,path,sizeof path);char *s=strrchr(path,'\\');if(s)strcpy(s+1,"native-crosshair.bin");else strcpy(path,"native-crosshair.bin");}
    FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"[crosshair] Original animation cache missing; choose Name Entry in launcher settings.\n");return 0;}
    char magic[4];int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"HC01",4);
    for(int i=0;i<2 && ok;i++){
        unsigned h[5];ok=fread(h,sizeof(unsigned),5,f)==5;
        if(!ok || h[0]<4 || h[0]>512 || h[1]<4 || h[1]>512 || h[2]<1 || h[2]>120 || h[3]<1 || h[3]>121 || h[4]!=10){ok=0;break;}
        NativeSightPlayer *p=&native_sight[i];p->width=h[0];p->height=h[1];p->idle=h[2];p->shot=h[3];
        size_t bytes=(size_t)p->width*p->height*4,count=(p->idle+p->shot)*10;
        p->pixels=malloc(bytes);p->frames=malloc(count*sizeof(*p->frames));
        ok=p->pixels && p->frames && fread(p->pixels,1,bytes,f)==bytes && fread(p->frames,sizeof(*p->frames),count,f)==count;
        for(size_t j=0;j<count && ok;j++){const float *v=(float*)&p->frames[j];for(int n=0;n<17;n++)if(!isfinite(v[n]) || fabsf(v[n])>4096)ok=0;}
    }
    if(ok && fgetc(f)!=EOF)ok=0;fclose(f);
    if(!ok){for(int i=0;i<2;i++){free(native_sight[i].pixels);free(native_sight[i].frames);memset(&native_sight[i],0,sizeof native_sight[i]);}fprintf(stderr,"[crosshair] Invalid original animation cache.\n");return 0;}
    native_sight_loaded=1;fprintf(stderr,"[crosshair] Original Name Entry textures and timelines loaded for P1/P2.\n");return 1;
}
static int native_sight_draw(float x,float y,float scale,int color)
{
    if(!native_sight_load())return 0;
    int player=crosshair_drawing_player?1:0;NativeSightPlayer *p=&native_sight[player];
    if(color<0 || color>8)color=0;
    if(player && color)color=color==2?1:2;
    if(!p->textures[color]){
        unsigned char *pixels=p->pixels,*tinted=NULL;
        if(color){
            static const unsigned char rgb[8][3]={{249,38,51},{45,161,255},{64,255,50},{255,225,45},{255,255,255},{40,255,255},{255,60,220},{255,140,30}};
            size_t len=(size_t)p->width*p->height*4;tinted=malloc(len);if(!tinted)return 0;
            for(size_t i=0;i<len;i+=4){unsigned char a=pixels[i],b=pixels[i+1],c=pixels[i+2],v=a>b?a:b;v=v>c?v:c;for(int j=0;j<3;j++)tinted[i+j]=(unsigned char)(v*rgb[color-1][j]/255);tinted[i+3]=pixels[i+3];}pixels=tinted;
        }
        glGenTextures(1,&p->textures[color]);glBindTexture(GL_TEXTURE_2D,p->textures[color]);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,p->width,p->height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);free(tinted);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    static unsigned shot_at[2];static int down[2],shot_active[2];
    unsigned now=SDL_GetTicks();int trigger=(host_cabinet_buttons() & (player?CAB_P2_TRIGGER:CAB_P1_TRIGGER))!=0;
    if(trigger && !down[player]){shot_at[player]=now;shot_active[player]=1;}down[player]=trigger;
    unsigned elapsed=(unsigned)(now-shot_at[player])*60/1000;
    int frame=now/ (1000.0/60.0);frame%=p->idle;
    if(shot_active[player] && elapsed<p->shot)frame=p->idle+elapsed;else shot_active[player]=0;
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,p->textures[color]);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    /* Native 60-unit ring maps to the existing reticle's 36px default ring. */
    scale*=.6f;
    for(int i=0;i<10;i++){
        NativeSightSprite *s=p->frames+frame*10+i;glBlendFunc(GL_SRC_ALPHA,s->add>0?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        glColor4fv(s->rgba);glBegin(GL_QUADS);
        for(int j=0;j<4;j++){glTexCoord2f(s->uv[j==1||j==2?2:0],s->uv[j>=2?3:1]);glVertex2f(x+s->xy[j*2]*scale,y+s->xy[j*2+1]*scale);}glEnd();
    }
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_TEXTURE_2D);return 1;
}
#endif
