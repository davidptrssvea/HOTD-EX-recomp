#ifndef HOTD4_CONTROLS_H
#define HOTD4_CONTROLS_H
enum { HC_PAUSE=11,HC_RESTART,HC_FULLSCREEN,HC_CROSSHAIR,HC_MOUSELOCK,HC_COUNT };
static const char *hc_keys[HC_COUNT]={"coin1","start1","trigger","reload","action","firemode","coin2","start2","test","service","mute","pause","restart","fullscreen","crosshair_toggle","mouse_lock"};
static const wchar_t *hc_labels[HC_COUNT]={L"Add Coin (P1)",L"Start (P1)",L"Shoot",L"Reload / Shake",L"Grenade",L"Fire Mode",L"Add Coin (P2)",L"Start (P2)",L"Test Menu",L"Service",L"Mute",L"Pause",L"Restart",L"Fullscreen",L"Toggle Crosshair",L"Mouse Lock"};
static const int hc_defaults[HC_COUNT]={'5','1',-1,-3,' ','f','6','2','t','s','m',SDLK_ESCAPE,SDLK_F6,SDLK_F11,SDLK_F2,SDLK_F1};
static int hc_loaded,hc_custom,hc_values[HC_COUNT];
static char hc_path[2048];
static void hc_init(void)
{
    if(hc_loaded)return;hc_loaded=1;
    memcpy(hc_values,hc_defaults,sizeof hc_values);
    const char*d=getenv("HOTD4_SETTINGS_DIR");
    if(d&&*d)snprintf(hc_path,sizeof hc_path,"%s/controls-profile.txt",d);
    else {GetModuleFileNameA(NULL,hc_path,sizeof hc_path);char*p=strrchr(hc_path,'\\');if(p)strcpy(p+1,"controls-profile.txt");else strcpy(hc_path,"controls-profile.txt");}
    /* The launcher runs from the dump's elf directory. Controls belong to
       the installed app's INI, never to files in the source arcade dump. */
    char ini[2048];snprintf(ini,sizeof ini,"%s",hc_path);char*a=strrchr(ini,'/'),*b=strrchr(ini,'\\'),*p=a&&(!b||a>b)?a:b;
    if(p)strcpy(p+1,"lindbergh.ini");else strcpy(ini,"lindbergh.ini");
    FILE*f=fopen(ini,"rb");char line[256],name[64],value[128];
    if(f){while(fgets(line,sizeof line,f)){if(sscanf(line," %63[^= ] = %127s",name,value)!=2)continue;for(int i=0;i<11;i++)if(!strcmp(name,hc_keys[i])){hc_values[i]=!strncmp(value,"mouse",5)?-atoi(value+5):!strcmp(value,"none")?0:!strcmp(value,"space")?32:!strcmp(value,"enter")?13:strlen(value)==1?(unsigned char)value[0]:atoi(value);}}fclose(f);}
    f=fopen(hc_path,"rb");
    if(f){if(fgets(line,sizeof line,f)&&!strncmp(line,"HOTD4-CONTROLS-1",16)){hc_custom=1;int v;while(fgets(line,sizeof line,f))if(sscanf(line,"%63s %d",name,&v)==2)for(int i=0;i<HC_COUNT;i++)if(!strcmp(name,hc_keys[i])&&v>=-5&&v<=0x10ffff+SDLK_SCANCODE_MASK&&!(i>=11&&v<=0))hc_values[i]=v;}fclose(f);}
    for(int i=0;i<11;i++)settings()->bind[i]=hc_values[i];
}
static int hc_get(int id){hc_init();return id>=0&&id<HC_COUNT?hc_values[id]:0;}
static int hc_matches(int id,int key){return key!=0&&hc_get(id)==key;}
static void hc_name(int id,wchar_t*out,int n)
{
    char text[100];settings_binding_name(hc_get(id),text,sizeof text);MultiByteToWideChar(CP_UTF8,0,text,-1,out,n);out[n-1]=0;
}
static int hc_set(int id,int value)
{
    hc_init();if(id<0||id>=HC_COUNT||value==0||(value<0&&(id<2||id>5)))return -1;
    for(int i=0;i<HC_COUNT;i++)if(i!=id&&hc_values[i]==value)return i+1;
    int old=hc_values[id];hc_values[id]=value;char temp[2100];snprintf(temp,sizeof temp,"%s.tmp",hc_path);
    FILE*f=fopen(temp,"wb");if(!f){hc_values[id]=old;return -2;}
    fputs("HOTD4-CONTROLS-1\n",f);for(int i=0;i<HC_COUNT;i++)fprintf(f,"%s %d\n",hc_keys[i],hc_values[i]);
    int failed=ferror(f);if(fclose(f))failed=1;
    if(failed||!MoveFileExA(temp,hc_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){hc_values[id]=old;return -2;}
    hc_custom=1;for(int i=0;i<11;i++)settings()->bind[i]=hc_values[i];return 0;
}
static int hc_reset(void)
{
    hc_init();int old[HC_COUNT];memcpy(old,hc_values,sizeof old);memcpy(hc_values,hc_defaults,sizeof hc_values);
    int status=hc_set(0,hc_values[0]);if(status){memcpy(hc_values,old,sizeof old);return status;}return 0;
}
#endif
