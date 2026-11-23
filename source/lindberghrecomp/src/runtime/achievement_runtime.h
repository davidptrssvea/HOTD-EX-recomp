/* Local achievements. No network service or PSN account is used. */
static const wchar_t *achievement_names[12]={L"ESCAPE",L"LOST",L"EMPTINESS",L"DESPAIR",L"REUNION",L"HOPE",L"EAT THIS!!!",L"CUT THE WIRE",L"BONUS EXPLORER",L"GET THE BUS",L"PANDORA'S BOX",L"TOP AGENT"};
static const wchar_t *achievement_descriptions[12]={
    L"Clear CHAPTER 1 on any difficulty.",
    L"Clear CHAPTER 2 on any difficulty.",
    L"Clear CHAPTER 3 on any difficulty.",
    L"Clear CHAPTER 4 on any difficulty.",
    L"Clear CHAPTER 5 on any difficulty.",
    L"Clear CHAPTER 6 on any difficulty.",
    L"During CHAPTER 1 Boss encounter, throw a grenade and damage JUSTICE.",
    L"During CHAPTER 2, cut the wire and let the enemies fall.",
    L"Find a secret room.",
    L"Shoot and destroy the BUS.",
    L"See all 4 endings of THE HOUSE OF THE DEAD 4.",
    L"Get RANK 2 or above in Final Result."
};
static SRWLOCK achievement_lock=SRWLOCK_INIT;
static unsigned long long achievement_times[12];
static unsigned achievement_endings;
static int achievement_loaded,achievement_queue[16],achievement_head,achievement_tail;
static void achievement_load_locked(void)
{
    if(achievement_loaded)return;achievement_loaded=1;
    char path[2048],magic[64];input_path(path,sizeof path,"achievements.txt");
    FILE *file=fopen(path,"rb");if(!file)return;
    if(fgets(magic,sizeof magic,file) && !strcmp(magic,"HOTD4-ACHIEVEMENTS-1\n")){
        unsigned endings=0;int id;unsigned long long timestamp;
        if(fscanf(file,"endings %u\n",&endings)==1)achievement_endings=endings&15;
        while(fscanf(file,"%d %llu\n",&id,&timestamp)==2)if(id>=0 && id<12)achievement_times[id]=timestamp;
    }
    fclose(file);
}
static void achievement_save_locked(void)
{
    char path[2048],tmp[2080];input_path(path,sizeof path,"achievements.txt");
    snprintf(tmp,sizeof tmp,"%s.%lu.tmp",path,GetCurrentProcessId());
    FILE *file=fopen(tmp,"wb");if(!file){fprintf(stderr,"[achievements] Cannot save progress\n");return;}
    fprintf(file,"HOTD4-ACHIEVEMENTS-1\nendings %u\n",achievement_endings);
    for(int i=0;i<12;i++)fprintf(file,"%d %llu\n",i,achievement_times[i]);
    int ok=!ferror(file);if(fclose(file))ok=0;
    if(!ok || !MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        fprintf(stderr,"[achievements] Progress save failed (%lu)\n",GetLastError());
}
static void achievement_unlock_locked(int id)
{
    if(id<0 || id>=12 || achievement_times[id])return;
    FILETIME time;GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks;ticks.LowPart=time.dwLowDateTime;ticks.HighPart=time.dwHighDateTime;
    achievement_times[id]=(ticks.QuadPart-116444736000000000ull)/10000000ull;
    int next=(achievement_head+1)%16;
    if(next!=achievement_tail){achievement_queue[achievement_head]=id;achievement_head=next;}
    achievement_save_locked();
    fprintf(stderr,"[achievements] Unlocked %d: %ls\n",id,achievement_names[id]);
}
void host_achievement_unlock(int id)
{
    AcquireSRWLockExclusive(&achievement_lock);achievement_load_locked();achievement_unlock_locked(id);ReleaseSRWLockExclusive(&achievement_lock);
}
void host_achievement_ending(int ending)
{
    if(ending<0 || ending>3)return;
    AcquireSRWLockExclusive(&achievement_lock);achievement_load_locked();
    unsigned before=achievement_endings;achievement_endings|=1u<<ending;
    if(before!=achievement_endings){achievement_save_locked();fprintf(stderr,"[achievements] Ending %d seen (mask %u)\n",ending,achievement_endings);}
    if(achievement_endings==15)achievement_unlock_locked(10);
    ReleaseSRWLockExclusive(&achievement_lock);
}
static GLuint achievement_star_texture(int metal)
{
    static GLuint textures[3];static int attempted[3];
    if(attempted[metal])return textures[metal];attempted[metal]=1;
    const char *files[]={"achievement-assets\\bronze.rgba","achievement-assets\\silver.rgba","achievement-assets\\gold.rgba"};
    char path[2048];input_path(path,sizeof path,files[metal]);FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned sizes[2];if(fread(sizes,sizeof sizes,1,f)!=1 || sizes[0]!=256 || sizes[1]!=256){fclose(f);return 0;}
    unsigned char *pixels=malloc(256*256*4);
    if(!pixels){fclose(f);return 0;}
    if(fread(pixels,256*256*4,1,f)==1){
        glGenTextures(1,&textures[metal]);pause_texture_setup(textures[metal]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    }
    free(pixels);fclose(f);return textures[metal];
}
static int achievement_toast_texture(GLuint texture,int id)
{
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=620;info.bmiHeader.biHeight=-136;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *pixels=NULL;HDC dc=CreateCompatibleDC(NULL);
    if(!dc)return 0;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    HFONT small=CreateFontW(-17,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    HFONT large=CreateFontW(-27,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    int ok=bitmap && pixels && small && large;
    if(ok){
        HGDIOBJ old=SelectObject(dc,bitmap),font=GetCurrentObject(dc,OBJ_FONT);SetBkMode(dc,TRANSPARENT);
        pause_fill(dc,0,0,620,136,RGB(18,19,24));pause_fill(dc,0,0,620,3,RGB(197,139,70));
        pause_text(dc,small,112,18,L"ACHIEVEMENT UNLOCKED",RGB(210,185,146));
        pause_text(dc,large,112,44,achievement_names[id],RGB(245,241,233));
        SelectObject(dc,small);SetTextColor(dc,RGB(203,199,193));
        RECT description={112,82,602,132};
        DrawTextW(dc,achievement_descriptions[id],-1,&description,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        GdiFlush();
        pause_texture_setup(texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,620,136,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
        SelectObject(dc,font);SelectObject(dc,old);
    }
    if(large)DeleteObject(large);if(small)DeleteObject(small);if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return ok;
}
static void achievement_draw(int w,int h)
{
    static int current=-1;static ULONGLONG until;static GLuint panel;
    ULONGLONG now=GetTickCount64();int changed=0;
    if(current>=0 && now>=until)current=-1;
    if(current<0){
        AcquireSRWLockExclusive(&achievement_lock);
        if(achievement_tail!=achievement_head){current=achievement_queue[achievement_tail];achievement_tail=(achievement_tail+1)%16;changed=1;until=now+5000;}
        ReleaseSRWLockExclusive(&achievement_lock);
    }
    if(current<0 || w<=0 || h<=0)return;
    GLint program,unpack;glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glUseProgram(0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for(int i=7;i>=0;i--){glActiveTexture(GL_TEXTURE0+i);glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_CUBE_MAP);}
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_BLEND);glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_VERTEX_PROGRAM_ARB);glDisable(GL_FRAGMENT_PROGRAM_ARB);glColorMask(1,1,1,1);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glViewport(0,0,w,h);glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,w,h,0,-1,1);glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    if(!panel)glGenTextures(1,&panel);
    if(changed)achievement_toast_texture(panel,current);
    GLuint star=achievement_star_texture(current==10?2:current==11?1:0);
    float scale=h/1080.0f;if(scale<0.65f)scale=0.65f;
    float x=w-660*scale,y=h*0.065f;
    glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glColor4f(1,1,1,1);
    glBindTexture(GL_TEXTURE_2D,panel);pause_quad(x,y,620*scale,136*scale,0);
    if(star){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glBindTexture(GL_TEXTURE_2D,star);pause_quad(x+10*scale,y+24*scale,88*scale,88*scale,0);}
    glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_TEXTURE);glPopMatrix();
    glPopClientAttrib();glPopAttrib();glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);glUseProgram((GLuint)program);
}

static int achievement_is_unlocked(int id)
{
    AcquireSRWLockExclusive(&achievement_lock);achievement_load_locked();int unlocked=id>=0 && id<12 && achievement_times[id]!=0;ReleaseSRWLockExclusive(&achievement_lock);return unlocked;
}
static int achievement_count(void)
{
    int count=0;for(int i=0;i<12;i++)count+=achievement_is_unlocked(i);return count;
}

static unsigned long long achievement_unlocked_time(int id)
{
    AcquireSRWLockExclusive(&achievement_lock);achievement_load_locked();
    unsigned long long time=id>=0 && id<12?achievement_times[id]:0;
    ReleaseSRWLockExclusive(&achievement_lock);return time;
}
static int achievement_ending_count(void)
{
    AcquireSRWLockExclusive(&achievement_lock);achievement_load_locked();
    unsigned mask=achievement_endings;ReleaseSRWLockExclusive(&achievement_lock);
    int count=0;for(int i=0;i<4;i++)if(mask&(1u<<i))count++;return count;
}
