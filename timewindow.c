/* =====================================================================
   时间窗 (TimeWindow) — Windows 单文件复刻版
   原生 C + Win32 + GDI+，无 Electron，单 EXE < 5MB。
   全面复刻 com.likpia.timewindow 悬浮窗应用：
     - 悬浮窗：置顶、无边框、半透明圆角/渐变背景、文字描边、拖拽、吸边、锁定、长按隐藏
     - 时间格式 DSL：{1}电量 {2}时间戳 {3..9}农历/干支/生肖 {10}场景 [expr,N] 表达式
     - 倒计时：秒/时点/当日/下月 场景，到时提示
     - 完整设置界面（复刻 activity_main）
   ===================================================================== */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wtypes.h>   /* 提供 PROPID，供 gdiplus.h 使用 */
#include <windowsx.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <winreg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")

using namespace Gdiplus;

/* ---------------- 小工具 ---------------- */
static void _sn(wchar_t* d, size_t n, const wchar_t* s) { if(n) { wcsncpy(d,s,n-1); d[n-1]=0; } }

static int is_letter(wchar_t c){ return (c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z'); }

/* Android 颜色 int(ARGB 有符号) -> Gdiplus ARGB */
static ARGB jcolor2argb(int c){
    unsigned v=(unsigned)c;
    int a=(int)((v>>24)&0xFF), r=(int)((v>>16)&0xFF), g=(int)((v>>8)&0xFF), b=(int)(v&0xFF);
    return Color::MakeARGB((BYTE)a,(BYTE)r,(BYTE)g,(BYTE)b);
}
/* 反显用于预览小色块 */
static COLORREF jcolor2cr(int c){ unsigned v=(unsigned)c; return RGB((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF); }

static int now_ms(void){ SYSTEMTIME st; GetLocalTime(&st);
    FILETIME ft; SystemTimeToFileTime(&st,&ft);
    ULARGE_INTEGER ul; ul.LowPart=ft.dwLowDateTime; ul.HighPart=ft.dwHighDateTime;
    /* 1601 基准毫秒，转 1970 */
    LONGLONG t=(LONGLONG)(ul.QuadPart/10000)-11644473600000LL; return (int)(t&0x7FFFFFFF); }

static LONGLONG unixtime_ms_now(void){
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul; ul.LowPart=ft.dwLowDateTime; ul.HighPart=ft.dwHighDateTime;
    return (LONGLONG)((ul.QuadPart/10000)-11644473600000LL);
}

/* 电池电量百分比(模拟 {1}) */
static int battery_level(void){
    SYSTEM_POWER_STATUS sp; if(GetSystemPowerStatus(&sp) && (sp.BatteryFlag&128)==0)
        return sp.BatteryLifePercent>=0&&sp.BatteryLifePercent<=100?sp.BatteryLifePercent:100;
    return 100;
}

/* MBCS->UTF-8 / UTF-8->WCHAR 便捷 */
static void utf8_to_wide(const char* s, wchar_t* o, size_t n){
    if(!s){ if(n)o[0]=0; return; }
    MultiByteToWideChar(CP_UTF8,0,s,-1,o,(int)n);
}
static void wide_to_utf8(const wchar_t* s, char* o, int n){
    if(!s){ if(n)o[0]=0; return; }
    WideCharToMultiByte(CP_UTF8,0,s,-1,o,n,NULL,NULL);
}

/* ---------------- 配置模块 (存 %APPDATA%\TimeWindow\config.ini，UTF-8) ---------------- */
static wchar_t cfg_path[MAX_PATH];
static char cfg_buf[32768];

static void cfg_resolve_path(void){
    if(cfg_path[0]) return;
    wchar_t dir[MAX_PATH];
    if(!GetEnvironmentVariableW(L"APPDATA",dir,MAX_PATH))
        if(!SHGetSpecialFolderPathW(NULL,dir,CSIDL_APPDATA,FALSE))
            GetWindowsDirectoryW(dir,MAX_PATH);
    wcscat(dir,L"\\TimeWindow");
    CreateDirectoryW(dir,NULL);
    swprintf(cfg_path,MAX_PATH,L"%ls\\config.ini",dir);
}
static void cfg_load(void){
    cfg_resolve_path();
    memset(cfg_buf,0,sizeof(cfg_buf));
    HANDLE h=CreateFileW(cfg_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD rd=0; ReadFile(h,cfg_buf,sizeof(cfg_buf)-1,&rd,NULL); CloseHandle(h); cfg_buf[rd]=0;
}
static void cfg_save(void){
    cfg_resolve_path();
    HANDLE h=CreateFileW(cfg_path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD wr=0; WriteFile(h,cfg_buf,(DWORD)strlen(cfg_buf),&wr,NULL); CloseHandle(h);
}
/* 查找 key= 的原始值字符串(指针, 不落盘缓存) */
static const char* cfg_raw(const char* key){
    size_t kl=strlen(key);
    char* line=cfg_buf; char* end=cfg_buf+strlen(cfg_buf);
    while(line<end){
        char* nl=strchr(line,'\n');
        size_t ln = nl? (size_t)(nl-line) : strlen(line);
        if(ln && line[ln-1]=='\r') ln--;
        if(ln>kl && strncmp(line,key,kl)==0 && line[kl]=='='){
            /* 返回 value（截断新行） */
            static char tmp[8192]; size_t vln=ln-kl-1; if(vln>=sizeof(tmp))vln=sizeof(tmp)-1;
            memcpy(tmp,line+kl+1,vln); tmp[vln]=0; return tmp;
        }
        line = nl? nl+1 : end;
    }
    return NULL;
}
static int cfg_get_int(const char* key,int def){ const char* v=cfg_raw(key); return v?atoi(v):def; }
static void cfg_set_int(const char* key,int val){
    char line[32]; sprintf(line,"%s=%d\n",key,val);
    /* 删除旧行 */
    char* p=strstr(cfg_buf,key);
    if(p){ char* e=strchr(p,'\n'); if(e){ char* rest=e+1; memmove(p,rest,strlen(rest)+1); } else *p=0; }
    strcat(cfg_buf,line);
}
/* 字符串值：读取时以 UTF-8；写入时转 UTF-8。内部用 UTF-8 char* */
static wchar_t g_strval[4096];
static const wchar_t* cfg_get_str(const char* key, const wchar_t* def){
    const char*v=cfg_raw(key);
    if(!v){ utf8_to_wide("", g_strval, 1); return def; }
    utf8_to_wide(v, g_strval, 4096);
    return g_strval;
}
static void cfg_set_str(const char* key, const wchar_t* val){
    char utf[4096]; wide_to_utf8(val?val:L"",utf,4096);
    char* p=strstr(cfg_buf,key);
    if(p){ char* e=strchr(p,'\n'); if(e){ memmove(p,e+1,strlen(e+1)+1);} else *p=0; }
    char line[4600]; snprintf(line,sizeof(line),"%s=%s\n",key,utf); strcat(cfg_buf,line);
}
static int cfg_get_color(const char* key,int def){ return cfg_get_int(key,def); }
static void cfg_set_color(const char* key,int val){ cfg_set_int(key,val); }

/* ---------------- 农历转换 (1900-2100) ---------------- */
static const int lunar_month_days[]={
//    1月2月...月长表 高字节闰月, 低字节每月天数   (农历 1900..2100)
0x4BD8,0x4AE0,0xA570,0x54D5,0xD260,0xD950,0x5554,0x56AF,0x9AD0,0x55D2,
0x4AE0,0xA5B6,0xA4D0,0xD250,0xD295,0xB550,0x56AA,0xADB5,0x55B0,0x4BA0,
0xA5B0,0x52D4,0xA950,0xE953,0xB2A0,0xB550,0xB550,0x55AA,0xEBA5,0x4BB0,
0xA570,0xB4D5,0xAA50,0xDAA4,0xB550,0xD6A0,0x45AA,0xB4AD,0xAB50,0xAAA5,
0xABD0,0xC550,0x55A5,0xABA0,0xA5B0,0xA5D4,0xA4D0,0xD250,0xD255,0xB550,
0xAB54,0xAAD0,0x4BB0,0xABD4,0xA4D0,0x4AD0,0xA5B0,0xA5B4,0xAB50,0xD4A0,
0x8AA5,0xAAA0,0xD2A0,0xD2D2,0xAA50,0x5520,0x56AA,0xDADA,0x6AA0,0xA5B0,
0xBAF5,0xAA50,0xD250,0xD295,0xB250,0xA550,0x5654,0xAA50,0xA5B0,0xA5B0,
0x54D5,0xAA50,0xD250,0xD250,0xEA94,0xDAA0,0x55B0,0xAB50,0xAAB5,0xAA50,
0xB550,0xABA0,0x4555,0xAAB0,0xAA50,0xA550,0x5AD4,0xAA50,0xD2A0,0xAA54,
0xAAD0,0xA550,0xAAD2,0xAA50,0x52B0,0x5AD5,0xB550,0xAA50,0xDAB4,0xB2A0,
0x55A0,0xAAA0,0xA592,0xAA50,0xB2A0,0xA550,0xAB54,0xAAB0,0xAB50,0xAAA5,
0xAAD0,0xD2A0,0xD2B4,0xD2A0,0xD250,0xD294,0xA550,0xAAA0,0xAAB0,0xAA50,
0x5554,0xAAA0,0xD2A0,0xD2B0,0xD5A0,0xB2B4,0xAB50,0xAA50,0xABA0,0xAAD2,
0xAA50,0x9550,0xAAD0,0xB2D0,0xB254,0xB550,0xA550,0xDAA0,0xAAD0,0xAADA,
0xAA50,0xAAA0,0xAAD0,0xAA54,0xAA50,0xAB50,0xB550,0xAAD0,0xDAB4,0xB2A0,
0x55A0,0xAAA0,0xAA92,0xD250,0xB250,0xAB54,0xAAD0,0xAA50,0xD2A0,0xABA0,
0xAA50,0x5554,0xAAA0,0xAAA0,0xAAB0,0xB2D0,0xD2D4,0xB550,0xA550,0xDAA0,
0xAA50,0xAAA0,0xAAD0,0xAA55,0xAAD0,0xAA50,0xAB50,0xB2D4,0xAA50,0xB550,
0x5AD4,0xAAA0,0xAAA0,0xB2A0,0xD2A0,0x52D6,0xAA50,0xAAA0,0xAA50,0xB2A0,
0xAAD0,0xAA52,0xAAA0,0xAAA0,0xAB50,0xB2D0,0xAA54,0xB250,0xAAD0,0xAB50,
0xAA50,0xAAA0,0xAAA0,0xB2D0,0xB2B4,0xAAA0,0xAA50,0xAAB0,0xAB50
};
static const wchar_t* shengxiao[12]={L"鼠",L"牛",L"虎",L"兔",L"龙",L"蛇",L"马",L"羊",L"猴",L"鸡",L"狗",L"猪"};
static const wchar_t* tian_gan[10]={L"甲",L"乙",L"丙",L"丁",L"戊",L"己",L"庚",L"辛",L"壬",L"癸"};
static const wchar_t* di_zhi[12]={L"子",L"丑",L"寅",L"卯",L"辰",L"巳",L"午",L"未",L"申",L"酉",L"戌",L"亥"};
static const wchar_t* lmonth_name[12]={L"一月",L"二月",L"三月",L"四月",L"五月",L"六月",L"七月",L"八月",L"九月",L"十月",L"冬月",L"腊月"};
static const wchar_t* lday_name[30]={L"初一",L"初二",L"初三",L"初四",L"初五",L"初六",L"初七",L"初八",L"初九",L"初十",
 L"十一",L"十二",L"十三",L"十四",L"十五",L"十六",L"十七",L"十八",L"十九",L"二十",
 L"廿一",L"廿二",L"廿三",L"廿四",L"廿五",L"廿六",L"廿七",L"廿八",L"廿九",L"三十"};
static int is_leap_g(int y){ return (y%4==0&&y%100!=0)||(y%400==0); }
static int days_in_year_g(int y){ return is_leap_g(y)?366:365; }

static int leap_month_of(int ry){ return lunar_month_days[ry-1900] & 0x0F; }
static int leap_days_of(int ry){
    return leap_month_of(ry) ? (((lunar_month_days[ry-1900] & 0x10000)!=0)?30:29) : 0;
}
static int month_days(int ry,int m){ return ((lunar_month_days[ry-1900] & (0x10000>>m))!=0)?30:29; }
static int lunar_year_days(int ry){
    int s=348,i;
    for(i=0x8000;i>0x8;i>>=1) if((lunar_month_days[ry-1900]&i)!=0) s+=1;
    return s+leap_days_of(ry);
}

/* 阳历 y/m/d -> 农历 y/m/d 及 是否闰月(输出 leapFlag) */
static void solar_to_lunar(int sy,int sm,int sd,int* ly,int* lm,int* ld,int* lLeap){
    struct tm st={0} , base={0};
    st.tm_year=sy-1900; st.tm_mon=sm-1; st.tm_mday=sd; st.tm_isdst=-1;
    base.tm_year=0; base.tm_mon=0; base.tm_mday=31; base.tm_isdst=-1;
    long offset=(long)( (mktime(&st)-mktime(&base))/86400 );
    if(offset<0){ *ly=*lm=*ld=*lLeap=0; return; }
    int i=1900, temp=0;
    for(i=1900;i<2100 && offset>=0;i++){ temp=lunar_year_days(i); offset-=temp; }
    if(offset<0){ offset+=temp; i--; }
    int year=i, leapM=leap_month_of(year), isLeap=0, m;
    for(m=1;m<=12 && offset>0;m++){
        if(leapM>0 && m==(leapM+1) && !isLeap){ m--; isLeap=1; temp=leap_days_of(year); }
        else temp=month_days(year,m);
        if(isLeap && m==(leapM+1)) isLeap=0;
        offset-=temp;
    }
    if(offset==0 && leapM>0 && m==(leapM+1)){ if(isLeap) isLeap=0; else { isLeap=1; m--; } }
    if(offset<0){ offset+=temp; m--; }
    *ly=year; *lm=m; *ld=(int)offset+1; *lLeap=isLeap;
}

/* 公历年 -> 天干/地支/生肖 索引
   1984 = 甲子。干支以农历新年为界，故取农历年 */
static int stem_idx(int lunarYear){ int n=lunarYear-4; return ((n%10)+10)%10; }
static int branch_idx(int lunarYear){ int n=lunarYear-4; return ((n%12)+12)%12; }

/* =====================================================================
   DSL 表达式求值：支持 + - * / × ÷ % ( )，忽略空格
   ===================================================================== */
typedef struct { double v; int isop, prec; wchar_t op; } tk;
static double popd(double* s,int* n){ return s[--(*n)]; }
static wchar_t popop(wchar_t* s,int* n){ if((*n)<=0)return 0; return s[--(*n)]; }
static int prec_of(wchar_t op){
    if(op==L'+'||op==L'-') return 1;
    if(op==L'*'||op==L'/'||op==0xD7||op==0xF7||op==L'%') return 2;
    return -1;
}
static void apply_op(double* s,int* n,wchar_t op){
    double b=popd(s,n); double a=popd(s,n); double r=0;
    switch(op){
        case L'+': r=a+b; break;
        case L'-': r=a-b; break;
        case L'*': case 0xD7: r=a*b; break;
        case L'/': case 0xF7: r=b==0?0:a/b; break;
        case L'%': r=b==0?0:fmod(a,b); break;
        default: r=0; break;
    }
    s[(*n)++]=r;
}
/* 返回 0=成功 */
static int eval_expr(const wchar_t* in, double* out){
    double num[128]; int nn=0;
    wchar_t ops[128]; int on=0;
    const wchar_t* p=in;
    int expect_num=1;
    while(*p){
        if(iswspace(*p)){ p++; continue; }
        if(iswdigit(*p)||*p==L'.'){
            double v=0; int fp=0; double frac=0.1;
            while(iswdigit(*p)||*p==L'.'){
                if(*p==L'.'){ fp=1; }
                else if(!fp){ v=v*10+(double)(*p-L'0'); }
                else { v=v+(*p-L'0')*frac; frac*=0.1; }
                p++;
            }
            num[nn++]=v; expect_num=0;
        }
        else if(*p==L'('){ ops[on++]=L'('; expect_num=1; p++; }
        else if(*p==L')'){
            int ok=0;
            while(on && ops[on-1]!=L'('){
                wchar_t op=popop(ops,&on); apply_op(num,&nn,op); ok=1;
            }
            if(on&&ops[on-1]==L'(') on--;
            (void)ok;
            expect_num=0; p++;
        }
        else { /* 运算符 */
            wchar_t op=*p;
            int prec;
            if(op==L'+'||op==L'-') prec=1;
            else if(op==L'*'||op==L'/'||op==0xD7||op==0xF7||op==L'%') prec=2;
            else return -1;
            /* 一元负号 */
            if(expect_num && (op==L'-')){ num[nn++]=0; }
            if(expect_num && (op==L'+')){ p++; continue; }
            if(expect_num && !(op==L'-')) return -1;
            while(on && ops[on-1]!=L'(' && prec<=prec_of(ops[on-1]))
                apply_op(num,&nn,popop(ops,&on));
            ops[on++]=op;
            expect_num=1; p++;
        }
    }
    while(on){ wchar_t op=popop(ops,&on); apply_op(num,&nn,op); }
    if(nn!=1){ return -1; }
    *out=num[0]; return 0;
}

/* =====================================================================
   时间格式渲染引擎（复刻 Android SimpleDateFormat + DSL）
   ===================================================================== */
typedef struct { int Y,M,D,h,m,s,S; int dow,doy; } DTState;

static int days_from_civil(int y,int m,int d){
    y-= m<=2;
    int era=(y>=0?y:y-399)/400;
    unsigned yoe=(unsigned)(y-era*400);
    unsigned doy=(153*(unsigned)(m+(m>2?-3:9))+2)/5+d-1;
    unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+(int)doe-719468;
}
static void civil_from_days(int z,int* y,int* m,int* d){
    z+=719468;
    int era=(z>=0?z:z-146096)/146097;
    unsigned doe=(unsigned)(z-era*146097);
    unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    int yy=(int)yoe+era*400; unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
    unsigned mp=(5*doy+2)/153; int dd=(int)(doy-(153*mp+2)/5+1);
    int mm=(int)(mp+(mp<10?3:-9)); *y=yy+(mm<=2); *m=mm; *d=dd;
}

static void resolve_dt(LONGLONG ms, DTState* t){
    LONGLONG secs=ms/1000; long millis=(long)(ms%1000); if(millis<0){millis+=1000;secs--;}
    __int64 days=secs/86400; long rem=(long)(secs%86400); if(rem<0){rem+=86400;days--;}
    int hh=rem/3600; int mmi=(rem%3600)/60; int ss=rem%60;
    int yi,mi,di; civil_from_days((int)days,&yi,&mi,&di);
    /* day of year */
    int doy = 1 + days_from_civil(yi,mi,di) - days_from_civil(yi,1,1);
    t->Y=yi; t->M=mi; t->D=di; t->h=hh; t->m=mmi; t->s=ss; t->S=(int)millis;
    /* 1970-01-01 为周四(下标4)；(days+4)%7 得星期 */
    t->dow = (int)(((days %7)+4)%7 + ((days%7)<0&&(days%7)<-4?7:0)); /* 0=周日 */
    t->doy=doy;
}
static const wchar_t* weekname[7]={L"星期日",L"星期一",L"星期二",L"星期三",L"星期四",L"星期五",L"星期六"};
static const wchar_t* week_short[7]={L"周日",L"周一",L"周二",L"周三",L"周四",L"周五",L"周六"};

/* 使用本地时区解析（时间显示用） */
static void resolve_dt_local(LONGLONG ms, DTState* t){
    time_t raw=(time_t)(ms/1000);
    struct tm lt; localtime_s(&lt,&raw);
    t->Y=lt.tm_year+1900; t->M=lt.tm_mon+1; t->D=lt.tm_mday;
    t->h=lt.tm_hour; t->m=lt.tm_min; t->s=lt.tm_sec; t->S=(int)(ms%1000); if(t->S<0)t->S+=1000;
    t->doy=lt.tm_yday+1;
    int dd=days_from_civil(t->Y,t->M,t->D);
    t->dow=((dd+4)%7+7)%7; /* 0=周日 */
}
/* 供 [expr] 内部变量 D/H/m/s/SSS 使用 */
static DTState g_curdt;

/* 用 ~sheet token 展开 pattern（记入 buf）。len: 可写字符数 */
static void format_simple(const wchar_t* pat,const DTState* t,wchar_t* out,size_t outsz){
    wchar_t* p=out; wchar_t* end=out+outsz-1;
    const wchar_t* q=pat;
    while(*q && p<end){
        wchar_t c=*q;
        if(!is_letter(c)){ *p++=c; q++; continue; }
        int k=0; while(q[k] && q[k]==c) k++;
        wchar_t tmp[64];
        switch(c){
            case L'y': if(k>=4) swprintf(tmp,64,L"%d",t->Y); else if(k==2) swprintf(tmp,64,L"%02d",t->Y%100); else swprintf(tmp,64,L"%d",t->Y); break;
            case L'M': if(k>=2) swprintf(tmp,64,L"%02d",t->M); else swprintf(tmp,64,L"%d",t->M); break;
            case L'd': if(k>=2) swprintf(tmp,64,L"%02d",t->D); else swprintf(tmp,64,L"%d",t->D); break;
            case L'H': if(k>=2) swprintf(tmp,64,L"%02d",t->h); else swprintf(tmp,64,L"%d",t->h); break;
            case L'h': { int h12=((t->h+11)%12)+1; if(k>=2) swprintf(tmp,64,L"%02d",h12); else swprintf(tmp,64,L"%d",h12); } break;
            case L'm': if(k>=2) swprintf(tmp,64,L"%02d",t->m); else swprintf(tmp,64,L"%d",t->m); break;
            case L's': if(k>=2) swprintf(tmp,64,L"%02d",t->s); else swprintf(tmp,64,L"%d",t->s); break;
            case L'S': {
                int n=k>9?9:(k<1?1:k);
                /* 毫秒固定为 3 位；S 数=1/2/3 时取前 n 位(十位/百分位/毫秒)，否则左补零到 n 位 */
                wchar_t s3[8]; swprintf(s3,8,L"%03d",(int)(t->S%1000));
                if(n<=3){ for(int i=0;i<n;i++)tmp[i]=s3[i]; tmp[n]=0; }
                else swprintf(tmp,64,L"%0*d",n,(int)(t->S%1000));
            } break;
            case L'a': wcscpy(tmp,(t->h<12)?L"上午":L"下午"); break;
            case L'D': if(k>=2) swprintf(tmp,64,L"%02d",t->doy); else swprintf(tmp,64,L"%d",t->doy); break;
            case L'E': wcscpy(tmp,(k<=3)?week_short[t->dow]:weekname[t->dow]); break;
            case L'n': case L'N': case L'G': case L'w': case L'W': case L'K': case L'z': case L'Z': case L'X': case L'u': case L'q': case L'Q': case L'g': case L'e': case L'c': case L'b': case L'B': case L'F': case L'k': case L'C': case L'Y': case L'l': case L'p': case L'V': case L'O': case L'T':
                { int i; for(i=0;i<k&&p<end;i++) *p++=c; q+=k; continue; }
            default: { int i; for(i=0;i<k&&p<end;i++) *p++=c; q+=k; continue; }
        }
        size_t tl=wcslen(tmp); if(p+tl>end) tl=(size_t)(end-p);
        memcpy(p,tmp,tl*sizeof(wchar_t)); p+=tl;
        q+=k;
    }
    *p=0;
}
/* 简易字符串替换（首次） */
static int wreplace(wchar_t* s,const wchar_t* from,const wchar_t* to){
    size_t fl=wcslen(from), tl=wcslen(to), sl=wcslen(s);
    wchar_t* fpos=wcsstr(s,from); if(!fpos) return 0;
    if(fl>=tl){ memmove(fpos+tl,fpos+fl,(sl-(size_t)(fpos-s)-fl+1)*sizeof(wchar_t)); memcpy(fpos,to,tl*sizeof(wchar_t)); }
    else {
        wchar_t buf[8192]; _sn(buf,8192,s);
        wchar_t* out2=(wchar_t*)malloc((sl+tl+1)*sizeof(wchar_t));
        size_t lead=(size_t)(fpos-s);
        memcpy(out2,buf,lead*sizeof(wchar_t));
        memcpy(out2+lead,to,tl*sizeof(wchar_t));
        wcscpy(out2+lead+tl,fpos+fl);
        wcscpy(s,out2); free(out2);
    }
    return 1;
}
/* 数字向下取整格式化：v → days 位小数截断(Java DecimalFormat DOWN) */
static void format_decimal(double v,int digits,wchar_t* out){
    if(digits<0){ /* 默认 double 转字符串 */
        if(v==(double)(long long)v) swprintf(out,8,L"%lld",(long long)v);
        else { swprintf(out,32,L"%.10g",v); }
        return;
    }
    double scale=pow(10.0,digits);
    double scaled=v*scale;
    double tr = (scaled>=0)? floor(scaled) : ceil(scaled);
    double r = tr/scale;
    /* 打印 digits 位 */
    wchar_t fmt[16]; swprintf(fmt,16,L"%%.%df",digits);
    swprintf(out,96,fmt,r);
    if(digits==0){ /* "0.0" 也可能出现，去掉 */ }
}
/* 统一渲染：给出最终显示文本 */
static wchar_t render_buf[8192];
static const wchar_t* render_display(void); /* 前向声明（在浮窗状态模块定义） */

/* =====================================================================
   浮窗状态 + 倒计时引擎
   ===================================================================== */
static struct {
    LONGLONG e;        /* 目标时长 ms */
    LONGLONG g;        /* 开始时刻 epoch ms */
    int running;       /* 倒计时是否进行中 */
} fl;

static const wchar_t* cfg_paused_text(void){ return cfg_get_str("pausedText", L"点击开始"); }
static const wchar_t* cfg_hint(void){ return cfg_get_str("countdownHint", L"时间到！"); }
static const wchar_t* cfg_pattern(void){ return cfg_get_str("pattern", L"HH:mm:ss"); }
static const wchar_t* cfg_countdown_pattern(void){ return cfg_get_str("countdownPattern",
    L"剩余{d}天{ph}时{pm}分{ps}秒"); }
static int cfg_scene(int d){ return cfg_get_int("sceneType",d); }
static int cfg_scene_seconds(int d){ return cfg_get_int("KEY_SCENE_TYPE_SET_SECOND",d); }

/* [expr,N] 表达式展开（单遍重建） */
static void expand_expressions(const wchar_t* s, wchar_t* out, size_t outsz){
    size_t o=0; const wchar_t* p=s;
    wchar_t* oend=out+outsz-1;
    while(*p && o<outsz-1){
        if(*p==L'['){
            /* 找最近的 ] */
            const wchar_t* close=wcschr(p+1,L']');
            if(close){
                wchar_t inner[1024]; size_t inlen=(size_t)(close-(p+1));
                if(inlen>=sizeof(inner)/sizeof(wchar_t)) inlen=sizeof(inner)/sizeof(wchar_t)-1;
                memcpy(inner,p+1,inlen*sizeof(wchar_t)); inner[inlen]=0;
                /* split at first ',' */
                wchar_t* comma=wcschr(inner,L',');
                wchar_t expr[1024]; int digits=-1;
                if(comma){
                    size_t el=(size_t)(comma-inner);
                    memcpy(expr,inner,el*sizeof(wchar_t)); expr[el]=0;
                    wchar_t* dd=comma+1; while(iswspace(*dd))dd++;
                    digits=*dd? _wtoi(dd):-1; if(digits>9)digits=9;
                } else {
                    _sn(expr,1024,inner);
                }
                /* 去空格 + 替换 D/H/m/s/SSS 变量后 eval */
                double v; wchar_t nexpr[1024]; int x=0;
                for(const wchar_t* q=expr;*q;q++){
                    if(!iswspace(*q)){
                        if((q[0]==L'S'&&q[1]==L'S'&&q[2]==L'S')){ x+=swprintf(nexpr+x,16,L"%d",g_curdt.S); q+=2; }
                        else if(q[0]==L'D'){ x+=swprintf(nexpr+x,16,L"%d",g_curdt.doy); }
                        else if(q[0]==L'H'){ x+=swprintf(nexpr+x,16,L"%d",g_curdt.h); }
                        else if(q[0]==L'm'){ x+=swprintf(nexpr+x,16,L"%d",g_curdt.m); }
                        else if(q[0]==L's'){ x+=swprintf(nexpr+x,16,L"%d",g_curdt.s); }
                        else nexpr[x++]=q[0];
                    }
                }
                nexpr[x]=0;
                if(eval_expr(nexpr,&v)==0){
                    wchar_t numtxt[128];
                    format_decimal(v,digits,numtxt);
                    size_t l=wcslen(numtxt);
                    size_t copyl = o+l;
                    while(copyl>outsz-1 && l>0){ l--; copyl--; }
                    memcpy(out+o,numtxt,l*sizeof(wchar_t)); o+=l;
                    p=close+1; continue;
                }
            }
        }
        out[o++]=*p++;
    }
    out[o]=0;
}

/* 变量替换 + [expr,N] 展开（时间&倒计时共用） */
static void finalize_display(wchar_t* s){
    int lyr,lm,ld,leap; int sti=0,bri=0;
    DTState dt; LONGLONG now=unixtime_ms_now();
    resolve_dt_local(now,&dt);
    g_curdt=dt;
    solar_to_lunar(dt.Y,dt.M,dt.D,&lyr,&lm,&ld,&leap);
    sti=stem_idx(lyr); bri=branch_idx(lyr);
    (void)leap;
    char bn[32]; snprintf(bn,sizeof(bn),"%d",battery_level());
    wchar_t wb[64]; utf8_to_wide(bn,wb,64);
    int i;
    wchar_t v10[48], v9[8], v8[8], v7[8], v6[8], v5[24], v4[8], v3[8], v2[32], v1[8];
    swprintf(v10,48,L"%d",cfg_scene_seconds(-1));
    wcscpy(v9,shengxiao[bri]); wcscpy(v8,di_zhi[bri]); wcscpy(v7,tian_gan[sti]);
    swprintf(v6,8,L"%d",days_in_year_g(dt.Y));
    wcscpy(v5,lday_name[ld-1]); wcscpy(v4,lmonth_name[lm-1]);
    swprintf(v3,8,L"%ls%ls",tian_gan[sti],di_zhi[bri]);
    swprintf(v2,32,L"%I64d",unixtime_ms_now());
    wcscpy(v1,wb);
    /* 执行替换（含 {1..9} 未出现时不存在损失） */
    struct { const wchar_t* a; const wchar_t* b; } RL[10] = {
        {L"{10}",v10},{L"{9}",v9},{L"{8}",v8},{L"{7}",v7},{L"{6}",v6},
        {L"{5}",v5},{L"{4}",v4},{L"{3}",v3},{L"{2}",v2},{L"{1}",v1},
    };
    for(i=0;i<10;i++){
        /* 避免 {1} 恰好字符串被 {10} 先替换造成误伤：{10}已先替换 */
        while(wcsstr(s,RL[i].a)) wreplace(s,RL[i].a,RL[i].b);
    }
    /* [expr,N] */
    wchar_t tmp[8192];
    _sn(tmp,8192,s);
    expand_expressions(tmp,s,8192);
}

/* 将字面 "\n" 转义为换行（便于在单行输入框/配置中表达多行模式） */
static void unescape_newlines(wchar_t* s){
    wchar_t* p=s; wchar_t* d=s;
    while(*p){
        if(p[0]==L'\\' && p[1]==L'n'){ *d++=L'\n'; p+=2; }
        else *d++=*p++;
    }
    *d=0;
}

/* 生成最终显示文本 */
static const wchar_t* render_display(void){
    int cd = cfg_get_int("countdown",0);
    LONGLONG now = unixtime_ms_now();
    wchar_t base[8192];
    if(cd){
        if(!fl.running){
            swprintf(base,8192,L"▶%ls",cfg_paused_text());
        } else {
            LONGLONG rem = fl.e - (now - fl.g);
            if(rem>0){
                LONGLONG tms=rem;
                long D=(long)(tms/86400000);
                long thhr=(long)(tms/3600000);
                long tmmin=(long)(tms/60000);
                long tsec=(long)(tms/1000);
                long phh=(long)((tms/3600000)%24);
                long pmm=(long)((tms/60000)%60);
                long pss=(long)((tms/1000)%60);
                long pmss=(long)(tms%1000);
                _sn(base,8192,cfg_countdown_pattern());
                /* 替换倒计时变量 */
                wchar_t tmp[32];
                struct { const wchar_t* a; LONG v; int z; } CV[] = {
                    {L"{d}",D,0},{L"{tms}",(LONG)tms,3},{L"{pms}",pmss,3},
                    {L"{th}",thhr,2},{L"{ph}",phh,2},{L"{ts}",tsec,2},
                    {L"{ps}",pss,2},{L"{tm}",tmmin,2},{L"{pm}",pmm,2},
                };
                int i; for(i=0;i<9;i++){
                    if(CV[i].z==3) swprintf(tmp,32,L"%03ld",CV[i].v);
                    else if(CV[i].z==2) swprintf(tmp,32,L"%02ld",CV[i].v);
                    else swprintf(tmp,32,L"%ld",CV[i].v);
                    while(wcsstr(base,CV[i].a)) wreplace(base,CV[i].a,tmp);
                }
            } else {
                fl.running=0;
                _sn(base,8192,cfg_hint());
            }
        }
    } else {
        LONGLONG t = now + cfg_get_int("time_offset",0);
        DTState dt; resolve_dt_local(t,&dt);
        format_simple(cfg_pattern(),&dt,base,8192);
        (void)cfg_countdown_pattern;
    }
    _sn(render_buf,8192,base);
    unescape_newlines(render_buf);
    finalize_display(render_buf);
    return render_buf;
}

/* 依据场景重新装载倒计时目标（e/g） */
static void setup_countdown_target(LONGLONG now){
    int scene=cfg_scene(0);
    if(scene==0){ fl.e=(LONGLONG)cfg_scene_seconds(10)*1000; }
    else if(scene==1){
        const wchar_t* hm=cfg_get_str("countdownTimeHHmm",L"22:00");
        int hh=_wtoi(hm), mm=0; const wchar_t* c=wcschr(hm,L':'); if(c) mm=_wtoi(c+1);
        /* 算今日该时刻 */
        LONGLONG daystart = now - (now%86400000);
        LONGLONG target = daystart + (LONGLONG)hh*3600000 + (LONGLONG)mm*60000;
        if(target<=now) target+=86400000;
        fl.e = target-now;
    }
    else if(scene==2){ fl.e = 86400000 - (now % 86400000); }
    else if(scene==3){
        DTState dt; resolve_dt(now,&dt);
        int dim = (dt.M==2)? (days_in_year_g(dt.Y)==366?29:28)
                 : ((dt.M==4||dt.M==6||dt.M==9||dt.M==11)?30:31);
        int remain_days = dim - dt.D;
        fl.e = (LONGLONG)remain_days*86400000 + (86400000 - (now%86400000));
    }
    else fl.e=10000;
    fl.g=now; fl.running=1;
}
static void start_countdown(void){ setup_countdown_target(unixtime_ms_now()); }
static void reset_countdown(void){ fl.running=0; fl.e=0; fl.g=-1; }

/* =====================================================================
   GDI+ 渲染：悬浮窗（分层窗口，逐像素 alpha）
   ===================================================================== */
static ULONG_PTR gdipToken=0;
static void gdi_start(void){ GdiplusStartupInput in; in.GdiplusVersion=1;
    in.DebugEventCallback=NULL; in.SuppressBackgroundThread=FALSE; in.SuppressExternalCodecs=TRUE;
    GdiplusStartup(&gdipToken,&in,NULL); }
static void gdi_stop(void){ GdiplusShutdown(gdipToken); }

/* 浮窗公共状态 */
static HWND hFloat=NULL;
static int g_gx=260,g_gy=200;      /* 屏幕位置 */
static int g_w=140,g_h=48;        /* 窗口尺寸 */
static int g_dragging=0,g_dragged=0;
static int g_hidden=0;            /* 长按隐藏 */

static int cfg_float_enabled(void){ return cfg_get_int("floatSwitch",0); }
static int cfg_lock(void){ return cfg_get_int("lockMode",0); }
static int cfg_edge(void){ return cfg_get_int("floatEdge",1); }
static int cfg_align(int d){ return cfg_get_int("align",d); }

static void show_settings(void);
static void tglFloatShow(void);
static void float_window_destroy(void);

static float screen_scale(void){
    HDC sdc=GetDC(NULL); int ly=GetDeviceCaps(sdc,LOGPIXELSY); ReleaseDC(NULL,sdc);
    return ly/96.0f;
}
static const wchar_t* cfg_fontfamily(void){ return cfg_get_str("fontFamily",L"Microsoft YaHei"); }
static float text_px(void){ return cfg_get_int("textSize",20)*screen_scale(); }

/* 读取渐变颜色数组（CSV ARGB，取配置，否则默认 e[0]） */
static int gradient_count(void);
static const wchar_t* cfg_gradient(void);

/* 生成圆角路径矩形 */
static GraphicsPath* make_round_rect(float W,float H,float r){
    GraphicsPath* p=new GraphicsPath();
    if(r>W/2)r=W/2; if(r>H/2)r=H/2;
    if(r<=0.5f){ p->AddRectangle(RectF(0,0,W,H)); return p; }
    float d=r*2;
    p->AddArc(RectF(0,0,d,d),180,90);
    p->AddArc(RectF(W-d,0,d,d),270,90);
    p->AddArc(RectF(W-d,H-d,d,d),0,90);
    p->AddArc(RectF(0,H-d,d,d),90,90);
    p->CloseFigure();
    return p;
}
/* 解析渐变颜色 to ARGB[] (最多 8) */
static int grad_colors(ARGB* out){
    int n=0; const wchar_t* gv=cfg_gradient();
    const wchar_t* p=gv; while(p && *p && n<8){
        while(*p==L','||iswspace(*p))p++;
        if(!*p)break;
        int v=(int)wcstol(p,NULL,0);
        unsigned u=(unsigned)v;
        out[n]=Color::MakeARGB((BYTE)((u>>24)&0xFF),(BYTE)((u>>16)&0xFF),(BYTE)((u>>8)&0xFF),(BYTE)(u&0xFF));
        n++;
        const wchar_t* c=wcschr(p,L','); if(!c)break; p=c+1;
    }
    return n;
}
static int gradient_count(void){ ARGB tmp[8]; return grad_colors(tmp); }
static const wchar_t* cfg_gradient(void){
    /* 默认 e[0] = ARGB(-9646594,-32836) */
    static const wchar_t* dfl=L"-9646594,-32836";
    const wchar_t* s=cfg_get_str("gradientColors",dfl);
    return s? s: dfl;
}

/* 用 GDI+ 绘制背景 + 文字到 Graphics */
static void draw_float_content(Graphics* g,int W,int H,float sc){
    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetTextRenderingHint(TextRenderingHintAntiAlias);
    g->Clear(Color(0,0,0,0));
    float corner=cfg_get_int("cornerVal",20)*sc;
    /* 背景：纯色 */
    ARGB bg=jcolor2argb(cfg_get_color("timeBgColor",-2013265920));
    int balpha=cfg_get_int("bgAlpha",0x88); if(balpha<0)balpha=0; if(balpha>255)balpha=255;
    bg&=0x00FFFFFFu; bg|=(ARGB)((unsigned)balpha<<24);
    GraphicsPath* path=make_round_rect((float)W,(float)H,corner);
    SolidBrush bb(bg);
    g->FillPath(&bb,path);
    /* 背景描边 */
    if(cfg_get_int("isStorkEnable",0)){
        int bw=cfg_get_int("borderWidth",2); if(bw<1)bw=1;
        Pen bp(jcolor2argb(cfg_get_color("timeBorderColor",-1)),(float)bw*sc);
        g->DrawPath(&bp,path);
    }
    delete path;
    /* 文字 */
    const wchar_t* txt=render_display();
    if(!txt||!txt[0]) return;
    int lr=cfg_get_int("timeTextPaddingLr",10)* (int)sc;
    int tb=cfg_get_int("timeTextPaddingTb",5)* (int)sc;
    if(lr<0)lr=0; if(tb<0)tb=0;
    RectF area((float)lr,(float)tb,(float)(W-2*lr),(float)(H-2*tb));
    Gdiplus::FontFamily fam(cfg_fontfamily());
    float fs=text_px();
    /* 颜色：colorType==1 渐变, else 纯色 */
    int ct=cfg_get_int("colorType",0);
    int textStrokeOn=cfg_get_int("isTextBorderEnable",0);
    StringFormat fmt;
    int al=cfg_align(1);
    fmt.SetAlignment((StringAlignment)(al<=0? StringAlignmentNear : (al==1?StringAlignmentCenter:StringAlignmentFar)));
    fmt.SetLineAlignment(StringAlignmentCenter);
    LPCWSTR fontSizeOk;
    (void)fontSizeOk;
    /* 用 path 绘制文本(带描边) */
    GraphicsPath* tp=new GraphicsPath();
    tp->AddString(txt,(INT)wcslen(txt),&fam,FontStyleRegular,fs,area,&fmt);
    if(textStrokeOn){
        int sw=cfg_get_int("textBorderWidth",5); if(sw<1)sw=1;
        Pen sp(jcolor2argb(cfg_get_color("textBorderColor",-1)),(float)sw);
        g->DrawPath(&sp,tp);
    }
    if(ct==1){
        ARGB cols[8]; int cn=grad_colors(cols); if(cn<1)cn=1;
        if(cn==1){ SolidBrush sb(cols[0]); g->FillPath(&sb,tp); }
        else {
            LinearGradientBrush lg(PointF(0,(float)H*5/6),PointF((float)W,(float)H/2),
                cols[0],cols[cn-1]);
            if(cn>2){
                Color ics[8]; REAL pos[8];
                for(int i=0;i<cn;i++){ ics[i]=Color(cols[i]); pos[i]= cn==1?0:(float)i/(cn-1); }
                lg.SetInterpolationColors(ics,pos,cn);
            }
            g->FillPath(&lg,tp);
        }
    } else {
        SolidBrush sb(jcolor2argb(cfg_get_color("timeTextColor",-1)));
        g->FillPath(&sb,tp);
    }
    delete tp;
}

/* 计算悬浮窗尺寸（依据文本测量） */
static void measure_float(int* W,int* H){
    float sc=screen_scale();
    int lr=cfg_get_int("timeTextPaddingLr",10)*(int)sc, tb=cfg_get_int("timeTextPaddingTb",5)*(int)sc;
    const wchar_t* txt=render_display();
    int l=wcslen(txt);
    RectF rc;
    if(l==0){ *W=lr*2+10*(int)sc; *H=tb*2+10*(int)sc; return; }
    do{
        Gdiplus::Font ff(cfg_fontfamily(),text_px(),FontStyleRegular,UnitPixel);
        Bitmap bmp(1,1); Graphics g(&bmp);
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.MeasureString(txt,l,&ff,PointF(0,0),&fmt,&rc);
    }while(0);
    int tw=(int)rc.Width+ (int)(8*screen_scale())+1, th=(int)rc.Height+(int)(4*screen_scale())+1;
    *W= tw + lr*2;
    *H= th + tb*2;
    int fixedW=cfg_get_int("widthVal",0);
    if(fixedW>0){ *W=(int)(fixedW*sc); if(*W<tw+lr*2)*W=tw+lr*2; }
    if(*W<16)*W=16; if(*H<10)*H=10;
}

/* 渲染并呈现分层窗口 */
static void present_float(void){
    if(!hFloat) return;
    measure_float(&g_w,&g_h);
    Bitmap bm(g_w,g_h,PixelFormat32bppPARGB);
    Graphics* g=Graphics::FromImage(&bm);
    draw_float_content(g,g_w,g_h,screen_scale());
    delete g;
    HDC sdc=GetDC(NULL);
    HBITMAP hbm=NULL; bm.GetHBITMAP(Color(0,0,0,0),&hbm);
    HDC memdc=CreateCompatibleDC(sdc);
    HBITMAP old=(HBITMAP)SelectObject(memdc,hbm);
    SIZE s={g_w,g_h}; POINT ps={0,0}, ppt={g_gx,g_gy};
    BLENDFUNCTION bf; bf.BlendOp=AC_SRC_OVER; bf.BlendFlags=0; bf.SourceConstantAlpha=255; bf.AlphaFormat=AC_SRC_ALPHA;
    UpdateLayeredWindow(hFloat,sdc,&ppt,&s,memdc,&ps,0,&bf,ULW_ALPHA);
    SelectObject(memdc,old);
    DeleteDC(memdc); DeleteObject(hbm); ReleaseDC(NULL,sdc);
}

/* 悬浮窗窗口过程 */
LRESULT CALLBACK FloatWndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    static POINT start; static int stx,sty; static BOOL down=FALSE; static DWORD downT=0;
    switch(m){
    case WM_LBUTTONDOWN:
        if(cfg_lock()) return 0;
        down=TRUE; g_dragging=1; g_dragged=0;
        SetCapture(h);
        GetCursorPos(&start); stx=g_gx; sty=g_gy; downT=GetTickCount();
        return 0;
    case WM_MOUSEMOVE:
        if(down && g_dragging){
            POINT p; GetCursorPos(&p);
            int dx=p.x-start.x, dy=p.y-start.y;
            if(!g_dragged && (abs(dx)>4 || abs(dy)>4)) g_dragged=1;
            if(g_dragged){
                g_gx=stx+dx; g_gy=sty+dy;
                RECT rc={0,0,0,0}; GetWindowRect(h,&rc); int ww=rc.right-rc.left, wh_=rc.bottom-rc.top;
                int sw_=GetSystemMetrics(SM_CXSCREEN), sh_=GetSystemMetrics(SM_CYSCREEN);
                if(g_gx<0)g_gx=0; if(g_gy<0)g_gy=0;
                if(g_gx+ww>sw_)g_gx=sw_-ww; if(g_gy+wh_>sh_)g_gy=sh_-wh_;
                SetWindowPos(h,NULL,g_gx,g_gy,ww,wh_,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if(down){
            down=FALSE; ReleaseCapture();
            g_dragging=0;
            BOOL longpress=( (GetTickCount()-downT)>=500 && !g_dragged );
            if(g_dragged){
                /* 吸边 */
                if(cfg_edge()){
                    RECT rc; GetWindowRect(h,&rc); int ww=rc.right-rc.left;
                    int sw_=GetSystemMetrics(SM_CXSCREEN);
                    int halfw=sw_/2;
                    g_gx = (g_gx+ww/2)<halfw ? 0 : sw_-ww;
                }
            } else if(!longpress){
                /* 点击：倒计时模式重新开始；时间模式打开设置 */
                if(cfg_get_int("countdown",0)){ start_countdown(); present_float(); }
                else show_settings();
            }
            if(GetCapture()==h) ReleaseCapture();
        }
        return 0;
    case WM_TIMER:
        /* 周期刷新悬浮窗文本 */
        present_float();
        return 0;
    case WM_RBUTTONDOWN:
        {   HMENU mu=CreatePopupMenu();
            AppendMenuW(mu,MF_STRING,4001,L"设置");
            AppendMenuW(mu,MF_STRING,4002,L"隐藏悬浮窗");
            AppendMenuW(mu,MF_STRING,4003,L"退出");
            POINT pt; GetCursorPos(&pt);
            int cmd=TrackPopupMenu(mu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,h,NULL);
            DestroyMenu(mu);
            if(cmd==4001){ show_settings(); return 0; }
            if(cmd==4002){ tglFloatShow(); return 0; }
            if(cmd==4003){ PostQuitMessage(0); return 0; }
        }
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

/* =====================================================================
   悬浮窗显隐 / 生命周期
   ===================================================================== */
static HINSTANCE hInst=0;
static HWND hSettings=NULL;
static void float_show(void){
    if(!hFloat){
        hFloat=CreateWindowExW(WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            L"TimeWindowFloat",L"时间窗",WS_POPUP,g_gx,g_gy,g_w,g_h,NULL,NULL,hInst,NULL);
        g_hidden=0;
    }
    if(g_hidden){ g_hidden=0; ShowWindow(hFloat,SW_SHOWNOACTIVATE); }
    int r=cfg_get_int("refresh",100); if(r<16)r=16; if(r>60000)r=60000;
    if(IsWindowVisible(hFloat)==FALSE) ShowWindow(hFloat,SW_SHOWNOACTIVATE);
    SetTimer(hFloat,1,r,NULL);
    present_float();
}
static void float_hide(void){
    if(hFloat){ KillTimer(hFloat,1); g_hidden=1; ShowWindow(hFloat,SW_HIDE); }
}
static void tglFloatShow(void){
    if(hFloat && IsWindowVisible(hFloat)) float_hide(); else float_show();
}
static void float_window_destroy(void){
    if(hFloat){ KillTimer(hFloat,1); DestroyWindow(hFloat); hFloat=NULL; }
}
static void settings_apply_scrollinfo(void);

static void show_settings(void){
    if(!hSettings){
        RECT wr; wr.left=0; wr.top=0; wr.right=(int)(430*screen_scale()); wr.bottom=(int)(700*screen_scale());
        AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW,FALSE,0);
        int W=wr.right-wr.left,H=wr.bottom-wr.top;
        int scw=GetSystemMetrics(SM_CXSCREEN),sch=GetSystemMetrics(SM_CYSCREEN);
        hSettings=CreateWindowExW(0,L"TimeWindowSettings",L"时间窗 设置",
            WS_OVERLAPPEDWINDOW|WS_VSCROLL,(scw-W)/2,(sch-H)/2<0?0:(sch-H)/2,W,H,NULL,NULL,hInst,NULL);
        settings_apply_scrollinfo();
    }
    ShowWindow(hSettings,SW_RESTORE);
    ShowWindow(hSettings,SW_SHOW);
    /* 悬浮窗为 NOACTIVATE，进程可能不在前台；绕过前置锁强制定位 */
    SetForegroundWindow(hSettings);
    SetWindowPos(hSettings,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    SetWindowPos(hSettings,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    SetFocus(hSettings);
    UpdateWindow(hSettings);
}

/* =====================================================================
   输入框（手写模态）
   ===================================================================== */
/* 全局 UI 字体（微软雅黑）：原生控件默认是系统点阵字体，必须显式设置 */
static HFONT g_uiFont=0;
static BOOL CALLBACK _apply_font_child(HWND c,LPARAM lp){ SendMessageW(c,WM_SETFONT,lp,TRUE); return TRUE; }
static void apply_ctrl_font(HWND h){ if(g_uiFont&&h) EnumChildWindows(h,_apply_font_child,(LPARAM)g_uiFont); }
static HWND g_ib=0,g_ibedit=0,g_ibparent=0; static int g_ibret=0; static wchar_t g_ibout[8192];
LRESULT CALLBACK InputWndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_COMMAND:
        if(LOWORD(w)==IDOK){ GetWindowTextW(g_ibedit,g_ibout,8192); g_ibret=1; DestroyWindow(h); return 0; }
        if(LOWORD(w)==IDCANCEL){ g_ibret=0; DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: g_ibret=0; DestroyWindow(h); return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
        { HDC dc=(HDC)w; SetBkMode(dc,TRANSPARENT); return (LRESULT)GetStockObject(WHITE_BRUSH); }
    case WM_NCDESTROY:
        g_ib=0; g_ibedit=0;
        if(g_ibparent){ EnableWindow(g_ibparent,TRUE); SetForegroundWindow(g_ibparent); InvalidateRect(g_ibparent,NULL,TRUE); }
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static int InputBox(HWND parent,const wchar_t* title,const wchar_t* init,wchar_t* out,int cap){
    double sc=screen_scale();
    int W=(int)(360*sc),H=(int)(160*sc);
    RECT p; GetWindowRect(parent?parent:GetDesktopWindow(),&p);
    int px=p.left+(p.right-p.left-W)/2, py=p.top+(p.bottom-p.top-H)/2;
    HWND h=CreateWindowExW(WS_EX_DLGMODALFRAME,L"TimeWindowInput",title,
        WS_POPUP|WS_CAPTION|WS_SYSMENU,px,py,W,H,NULL,NULL,hInst,NULL);
    g_ibparent=parent; g_ib=h; g_ibret=0;
    g_ibedit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",init,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
        (int)(12*sc),(int)(22*sc),(int)(W-24*sc),(int)(32*sc),h,(HMENU)100,hInst,NULL);
    SetWindowTheme(g_ibedit,L"explorer",NULL);
    CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
        (int)(W-180*sc),(int)(H-56*sc),(int)(76*sc),(int)(32*sc),h,(HMENU)IDOK,hInst,NULL);
    SetWindowTheme(GetDlgItem(h,IDOK),L"explorer",NULL);
    CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        (int)(W-92*sc),(int)(H-56*sc),(int)(80*sc),(int)(32*sc),h,(HMENU)IDCANCEL,hInst,NULL);
    SetWindowTheme(GetDlgItem(h,IDCANCEL),L"explorer",NULL);
    if(parent) EnableWindow(parent,FALSE);
    apply_ctrl_font(h);
    SendMessageW(g_ibedit,WM_SETTEXT,0,(LPARAM)init);
    SendMessageW(g_ibedit,EM_SETSEL,0,-1);
    SetFocus(g_ibedit); SetForegroundWindow(h); ShowWindow(h,SW_SHOWNORMAL);
    MSG msg;
    while(g_ib && GetMessageW(&msg,NULL,0,0)>0){ TranslateMessage(&msg); DispatchMessageW(&msg); }
    if(out && g_ibret) wcscpy(out,g_ibout);
    else if(out) out[0]=0;
    return g_ibret;
}

/* =====================================================================
   格式选择对话框：预设选择 + 自定义格式 + 完整格式说明
   （用于 “时间格式” 与 “倒计时格式”）
   ===================================================================== */
static HWND g_ff=0,g_ffedit=0; static int g_ffret=0; static wchar_t g_ffout[8192];
static const wchar_t* const* g_ffpresets=NULL; static int g_ffpcount=0;

static const wchar_t* const fmt_presets_time[]={
    L"HH:mm:ss", L"HH:mm:ss.SSS", L"HH时mm分ss秒",
    L"yyyy-MM-dd HH:mm:ss", L"EEEE MM月dd日 HH:mm", L"ahh:mm:ss"
};
static const wchar_t* const fmt_presets_cd[]={
    L"剩余{d}天{ph}时{pm}分{ps}秒", L"剩余{ph}:{pm}:{ps}",
    L"HH:mm:ss", L"剩余{ts}秒"
};
static const wchar_t* fmt_doc_time=
    L"支持 Java SimpleDateFormat 风格：\r\n"
    L"y 年：yyyy=2026，yy=26\r\n"
    L"M 月：MM=09，M=9\r\n"
    L"d 日：dd=05，d=5\r\n"
    L"H 24小时：HH=14\r\n"
    L"h 12小时：hh=02\r\n"
    L"m 分：mm=30，s 秒：ss=45\r\n"
    L"S 毫秒：S=百毫秒(1位)，SS=十毫秒(2位)，SSS=毫秒(3位)\r\n"
    L"a 上午/下午，D 年中的第几天\r\n"
    L"E 星期：EEE=周五，EEEE=星期五\r\n"
    L"其余字母按原样显示\r\n"
    L"示例：HH:mm:ss.SSS → 09:30:45.123";
static const wchar_t* fmt_doc_cd=
    L"倒计时专用占位符：\r\n"
    L"{d}   剩余天数\r\n"
    L"{th}  剩余总小时\r\n"
    L"{tm}  剩余总分钟\r\n"
    L"{ts}  剩余总秒数\r\n"
    L"{ph}  整小时\r\n"
    L"{pm}  分钟（不足60）\r\n"
    L"{ps}  秒（不足60）\r\n"
    L"{pms} 毫秒\r\n"
    L"{tms} 剩余总毫秒\r\n"
    L"[表达式,N] 计算结果并保留N位小数\r\n"
    L"也支持上方“时间格式”的通用符（如 HH:mm:ss）\r\n"
    L"示例：剩余{d}天{ph}时{pm}分{ps}秒";

LRESULT CALLBACK FmtWndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND){
        int id=LOWORD(w);
        if(id==IDOK){ GetWindowTextW(g_ffedit,g_ffout,8192); g_ffret=1; DestroyWindow(h); return 0; }
        if(id==IDCANCEL){ g_ffret=0; DestroyWindow(h); return 0; }
        if(id>=200 && id<240){ int k=id-200; if(k>=0&&k<g_ffpcount){ SendMessageW(g_ffedit,WM_SETTEXT,0,(LPARAM)g_ffpresets[k]); SetFocus(g_ffedit); SendMessageW(g_ffedit,EM_SETSEL,0,-1);} return 0; }
    }
    if(m==WM_CLOSE){ g_ffret=0; DestroyWindow(h); return 0; }
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN||m==WM_CTLCOLOREDIT){
        HDC dc=(HDC)w; SetBkMode(dc,TRANSPARENT); return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    if(m==WM_NCDESTROY){
        g_ff=0; g_ffedit=0;
        HWND p=(HWND)GetWindowLongPtrW(h,GWLP_USERDATA);
        if(p&&IsWindow(p)){ EnableWindow(p,TRUE); SetForegroundWindow(p); InvalidateRect(p,NULL,TRUE); }
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static int FormatDialog(HWND parent,const wchar_t* title,int isCountdown,const wchar_t* init,wchar_t* out,int cap){
    double sc=screen_scale();
    g_ffpresets=isCountdown?fmt_presets_cd:fmt_presets_time;
    g_ffpcount =isCountdown?(int)(sizeof(fmt_presets_cd)/sizeof(fmt_presets_cd[0]))
                          :(int)(sizeof(fmt_presets_time)/sizeof(fmt_presets_time[0]));
    const wchar_t* doc=isCountdown?fmt_doc_cd:fmt_doc_time;
    int W=(int)(650*sc),H=(int)(600*sc);
    RECT p; GetWindowRect(parent?parent:GetDesktopWindow(),&p);
    int px=p.left+(p.right-p.left-W)/2, py=p.top+(p.bottom-p.top-H)/2;
    HWND h=CreateWindowExW(WS_EX_DLGMODALFRAME,L"TimeWindowFmt",title,
        WS_POPUP|WS_CAPTION|WS_SYSMENU,px,py,W,H,NULL,NULL,hInst,NULL);
    SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)parent);
    g_ff=h; g_ffret=0;
    /* 模板标题 + 编辑框 */
    CreateWindowW(L"STATIC",L"格式模板：",WS_CHILD|WS_VISIBLE,(int)(12*sc),(int)(8*sc),(int)(140*sc),(int)(20*sc),h,NULL,hInst,NULL);
    g_ffedit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",init,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
        (int)(12*sc),(int)(30*sc),(int)(W-24*sc),(int)(32*sc),h,(HMENU)300,hInst,NULL);
    SetWindowTheme(g_ffedit,L"explorer",NULL);
    /* 预设按钮（两列，加宽按钮并缩短间距避免长文本溢出） */
    CreateWindowW(L"STATIC",L"预设格式（点击填入）：",WS_CHILD|WS_VISIBLE,(int)(12*sc),(int)(74*sc),(int)(300*sc),(int)(20*sc),h,NULL,hInst,NULL);
    int cols=2,bw=(int)((W-52*sc)/2),bh=(int)(30*sc),pyy=(int)(100*sc);
    for(int i=0;i<g_ffpcount;i++){
        int cx=(int)(12*sc)+(i%cols)*(bw+(int)(12*sc));
        int cy=pyy+(i/cols)*(bh+(int)(6*sc));
        HWND btn=CreateWindowW(L"BUTTON",g_ffpresets[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            cx,cy,bw,bh,h,(HMENU)(INT_PTR)(200+i),hInst,NULL);
        SetWindowTheme(btn,L"explorer",NULL);
    }
    int presetRows=(g_ffpcount+cols-1)/cols;
    int legendTop=pyy+presetRows*(bh+(int)(6*sc))+(int)(10*sc);
    /* 格式说明 */
    CreateWindowW(L"STATIC",L"格式说明：",WS_CHILD|WS_VISIBLE,(int)(12*sc),legendTop,(int)(160*sc),(int)(20*sc),h,NULL,hInst,NULL);
    CreateWindowW(L"STATIC",doc,WS_CHILD|WS_VISIBLE|SS_LEFT,
        (int)(12*sc),legendTop+(int)(22*sc),(int)(W-24*sc),H-legendTop-(int)(90*sc),h,NULL,hInst,NULL);
    /* 确定 / 取消 */
    CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
        (int)(W-180*sc),H-(int)(52*sc),(int)(76*sc),(int)(32*sc),h,(HMENU)IDOK,hInst,NULL);
    SetWindowTheme(GetDlgItem(h,IDOK),L"explorer",NULL);
    CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        (int)(W-92*sc),H-(int)(52*sc),(int)(80*sc),(int)(32*sc),h,(HMENU)IDCANCEL,hInst,NULL);
    SetWindowTheme(GetDlgItem(h,IDCANCEL),L"explorer",NULL);
    if(parent) EnableWindow(parent,FALSE);
    apply_ctrl_font(h);
    SendMessageW(g_ffedit,WM_SETTEXT,0,(LPARAM)init);
    SendMessageW(g_ffedit,EM_SETSEL,0,-1);
    SetFocus(g_ffedit); SetForegroundWindow(h); ShowWindow(h,SW_SHOWNORMAL);
    MSG msg;
    while(g_ff && GetMessageW(&msg,NULL,0,0)>0){ TranslateMessage(&msg); DispatchMessageW(&msg); }
    if(out&&g_ffret) wcscpy(out,g_ffout); else if(out) out[0]=0;
    return g_ffret;
}

/* =====================================================================
   设置界面（自绘，复刻 activity_main）
   ===================================================================== */
static int g_scroll=0; static int g_hover=-1;
static int wheel_acc=0;   /* 滚轮累积量，支持高精度/触摸板小增量 */
/* 读取 Windows 系统“一次滚动下列行数”(滚轮相关)设置；0 表示整页滚动 */
static int sys_wheel_lines(void){
    DWORD v=0,bcb=sizeof(v);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",L"WheelScrollLines",
        RRF_RT_REG_DWORD,NULL,&v,&bcb)==ERROR_SUCCESS) return (int)v;
    return 3;  /* 读取失败时按 Windows 默认 3 行 */
}
#define WM_TRAY (WM_APP+1)
static void tray_add(void);   /* 前向声明：设置窗口需在 Explorer 重建时重加托盘图标 */
static UINT g_msgTaskbar=0;   /* TaskbarCreated 系统消息，Explorer 崩溃/重启后广播 */
static void pick_color(HWND parent,const char* key,int def);
static const wchar_t* cyc_align[]={L"左",L"中",L"右"};
static const wchar_t* cyc_scene[]={L"按秒倒计时",L"今日特定时刻",L"今日结束",L"本月结束"};
static const wchar_t* cyc_ct[]={L"纯色",L"渐变"};
enum{ R_SECTION=0,R_SWITCH=1,R_TEXT=2,R_COLOR=3,R_CYCLE=4,R_ACTION=5 };
typedef struct{ int type; const char* key; const wchar_t* title; const wchar_t* sub;
                int dv; const wchar_t* ds; const wchar_t** cyc; int nc; } SRow;
static SRow rows[]={
 {R_SECTION,NULL,L"　悬浮窗",NULL,0,NULL,NULL,0},
 {R_SWITCH,"floatSwitch",L"悬浮窗开关",L"显示屏幕置顶悬浮时钟",1,NULL,NULL,0},
 {R_SWITCH,"countdown",L"模式：倒计时",L"开启显示倒计时，关闭显示时间",0,NULL,NULL,0},
 {R_CYCLE,"sceneType",L"倒计时场景",L"按秒 / 特定时刻 / 今日 / 本月",0,NULL,cyc_scene,4},
 {R_TEXT,"countdownPattern",L"倒计时格式",L"支持 {d}{th}{tm}{ts}{ph}{pm}{ps}{pms}{tms} 及 [表达式,N]" ,0,L"剩余{d}天{ph}时{pm}分{ps}秒",NULL,0},
 {R_TEXT,"countdownHint",L"结束时提示",L"倒计时结束显示的文字",0,L"时间到！",NULL,0},
 {R_TEXT,"pattern",L"时间格式",L"SimpleDateFormat，如 HH:mm:ss",0,L"HH:mm:ss",NULL,0},
 {R_SECTION,NULL,L"　浮动行为",NULL,0,NULL,NULL,0},
 {R_SWITCH,"floatEdge",L"自动吸附",L"拖到屏幕边缘自动贴边",1,NULL,NULL,0},
 {R_SWITCH,"lockMode",L"锁定",L"锁定后不可拖动/编辑",0,NULL,NULL,0},
 {R_TEXT,"refresh",L"刷新频率(毫秒)",L"悬浮窗刷新间隔，默认 100",0,L"100",NULL,0},
 {R_CYCLE,"align",L"时间对齐",L"文字水平对齐 左/中/右",1,NULL,cyc_align,3},
 {R_SECTION,NULL,L"　字体样式",NULL,0,NULL,NULL,0},
 {R_TEXT,"fontFamily",L"字体",L"如 Microsoft YaHei / Consolas",0,L"Microsoft YaHei",NULL,0},
 {R_TEXT,"textSize",L"字号",L"悬浮文字大小",0,L"20",NULL,0},
 {R_CYCLE,"colorType",L"文字着色方式",L"纯色或渐变着色",0,NULL,cyc_ct,2},
 {R_COLOR,"timeTextColor",L"文字颜色",L"纯色模式的文字颜色",-1,NULL,NULL,0},
 {R_TEXT,"gradientColors",L"渐变颜色(CSV)",L"ARGB 列表，如 -9646594,-32836",0,L"-9646594,-32836",NULL,0},
 {R_SWITCH,"isTextBorderEnable",L"文字描边",L"给文字描边",0,NULL,NULL,0},
 {R_TEXT,"textBorderWidth",L"描边宽度",L"像素",0,L"5",NULL,0},
 {R_COLOR,"textBorderColor",L"描边颜色",L"文字描边颜色",-1,NULL,NULL,0},
 {R_SECTION,NULL,L"　背景样式",NULL,0,NULL,NULL,0},
 {R_COLOR,"timeBgColor",L"背景颜色",L"悬浮窗背景颜色",-2013265920,NULL,NULL,0},
 {R_TEXT,"bgAlpha",L"背景不透明度",L"0(全透明)~255(不透明)，越小越透",0,L"136",NULL,0},
 {R_SWITCH,"isStorkEnable",L"背景描边",L"给背景描边框",0,NULL,NULL,0},
 {R_TEXT,"borderWidth",L"背景描边宽度",L"像素",0,L"2",NULL,0},
 {R_COLOR,"timeBorderColor",L"背景描边颜色",L"背景边框颜色",-1,NULL,NULL,0},
 {R_TEXT,"cornerVal",L"圆角",L"背景圆角半径",0,L"20",NULL,0},
 {R_TEXT,"widthVal",L"悬浮窗宽度",L"0=随文字自适应",0,L"0",NULL,0},
 {R_TEXT,"timeTextPaddingTb",L"上下边距",L"文字距上下边界",0,L"5",NULL,0},
 {R_TEXT,"timeTextPaddingLr",L"左右边距",L"文字距左右边界",0,L"10",NULL,0},
 {R_SECTION,NULL,L"　主题（主界面）",NULL,0,NULL,NULL,0},
 {R_COLOR,"uiBgColor",L"主界面背景色",L"设置窗口的背景颜色",0xF8FAFC,NULL,NULL,0},
 {R_COLOR,"uiSectionColor",L"分区标题背景",L"分区标题条的背景颜色",0xEDF1F7,NULL,NULL,0},
 {R_SECTION,NULL,L"　其他",NULL,0,NULL,NULL,0},
 {R_ACTION,"__pos",L"重置位置",L"将悬浮窗移回屏幕中央",0,NULL,NULL,0},
};
static int nrow=(int)(sizeof(rows)/sizeof(rows[0]));
#define RSH 30
#define RRH 48

static int row_h(int i){ return (int)((rows[i].type==R_SECTION?RSH:RRH)*screen_scale()+0.5f); }
static int layout_height(void){ int h=0,i; for(i=0;i<nrow;i++)h+=row_h(i); return h; }
/* 钳制滚动位置并把滚动范围/位置同步到原生纵向滚动条 */
static void settings_apply_scrollinfo(void){
    if(!hSettings) return;
    RECT rc; GetClientRect(hSettings,&rc);
    int maxScroll=layout_height()-(rc.bottom-rc.top); if(maxScroll<0)maxScroll=0;
    if(g_scroll<0)g_scroll=0; if(g_scroll>maxScroll)g_scroll=maxScroll;
    SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};
    si.nMin=0; si.nMax=layout_height()-1; si.nPage=(rc.bottom-rc.top); si.nPos=g_scroll;
    SetScrollInfo(hSettings,SB_VERT,&si,TRUE);
}
static int row_at(int y, int* within){
    int i, yy=-g_scroll;
    for(i=0;i<nrow;i++){ int rh=row_h(i); if(y>=yy && y<yy+rh){ if(within)*within=y-yy; return i; } yy+=rh; }
    return -1;
}
static const wchar_t* row_value(const SRow* r,wchar_t* buf,int cap){
    switch(r->type){
    case R_SWITCH: return cfg_get_int(r->key,r->dv)?L"开":L"关";
    case R_TEXT:   return cfg_get_str(r->key,r->ds);
    case R_COLOR: { int v=cfg_get_int(r->key,r->dv)&0xFFFFFF; swprintf(buf,cap,L"  #%06X",(unsigned)v); return buf; }
    case R_CYCLE: { int v=cfg_get_int(r->key,r->dv); if(v<0||v>=r->nc)v=0; return r->cyc[v]; }
    }
    return L"";
}
static void draw_switch(HDC hdc,int x,int y,int w,int hh,int on){
    RECT r={x,y,x+w,y+hh};
    HRGN rg=CreateRoundRectRgn(x,y,x+w+1,y+hh+1,hh/2,hh/2);
    HBRUSH b=CreateSolidBrush(on?0x00FF9A4C:0x00D0D0D3); /* 蓝/灰 */
    FillRgn(hdc,rg,b); DeleteObject(b); DeleteObject(rg);
    int k=hh-4, kx=on? x+w-hh+2 : x+2;
    HBRUSH wk=CreateSolidBrush(0xFFFFFF);
    Ellipse(hdc,kx,y+2,kx+k,y+2+k); DeleteObject(wk);
}
static void draw_settings_surface(void){
    PAINTSTRUCT ps; HDC dst=BeginPaint(hSettings,&ps);
    RECT rc; GetClientRect(hSettings,&rc);
    /* 双缓冲：先在内存画完再一次 BitBlt，彻底消除滚动/重绘频闪 */
    int cw=rc.right-rc.left, ch=rc.bottom-rc.top;
    HDC hdc=CreateCompatibleDC(dst);
    HBITMAP bm=CreateCompatibleBitmap(dst,cw,ch);
    HGDIOBJ ob=SelectObject(hdc,bm);
    SetBkMode(hdc,TRANSPARENT);
    HBRUSH bg=CreateSolidBrush(cfg_get_int("uiBgColor",0xF8FAFC) & 0xFFFFFF); FillRect(hdc,&rc,bg); DeleteObject(bg);
    int totalH=layout_height(), viewH=rc.bottom-rc.top;
    int maxScroll=totalH>viewH?totalH-viewH:0; if(g_scroll<0)g_scroll=0; if(g_scroll>maxScroll)g_scroll=maxScroll;
    HFONT fT=CreateFontW(-(int)(16*screen_scale()),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei");
    HFONT fS=CreateFontW(-(int)(11*screen_scale()),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei");
    HFONT fH=CreateFontW(-(int)(13*screen_scale()),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei");
    int i, y=-g_scroll;
    for(i=0;i<nrow;i++){
        int rh=row_h(i), yy=y;
        int skip=(yy+rh<0)||(yy>rc.bottom);
        if(!skip){
            if(rows[i].type==R_SECTION){
                RECT sr={0,yy,rc.right,yy+rh};
                HBRUSH sb=CreateSolidBrush(cfg_get_int("uiSectionColor",0xEDF1F7) & 0xFFFFFF); FillRect(hdc,&sr,sb); DeleteObject(sb);
                SetTextColor(hdc,0x334B74);
                SelectObject(hdc,fH);
                DrawTextW(hdc,rows[i].title,-1,&sr,DT_VCENTER|DT_SINGLELINE|DT_LEFT);
            } else {
                int hover=(i==g_hover);
                if(hover){ HBRUSH hb=CreateSolidBrush(0xEEF3FB); RECT hr={0,yy,rc.right,yy+rh}; FillRect(hdc,&hr,hb); DeleteObject(hb); }
                RECT tr={14,yy+ (int)(6*screen_scale()),rc.right,yy+(int)(26*screen_scale())};
                SelectObject(hdc,fT); SetTextColor(hdc,0x17181C);
                DrawTextW(hdc,rows[i].title,-1,&tr,DT_VCENTER|DT_SINGLELINE|DT_LEFT);
                if(rows[i].sub){
                    /* 说明文字止于右侧数值列之前，避免与数值重叠 */
                    int subR=rc.right;
                    if(rows[i].type==R_TEXT||rows[i].type==R_CYCLE)
                        subR=rc.right-(int)(185*screen_scale());
                    RECT sr2={14,yy+(int)(27*screen_scale()),subR,yy+rh-(int)(2*screen_scale())};
                    SelectObject(hdc,fS); SetTextColor(hdc,0x8A8A8E);
                    DrawTextW(hdc,rows[i].sub,-1,&sr2,DT_VCENTER|DT_SINGLELINE|DT_LEFT); }
                /* 右侧 */
                if(rows[i].type==R_SWITCH){ draw_switch(hdc,rc.right-(int)(58*screen_scale()),yy+(rh-(int)(24*screen_scale()))/2,(int)(48*screen_scale()),(int)(24*screen_scale()),cfg_get_int(rows[i].key,rows[i].dv)); }
                else {
                    wchar_t vb[4096]; const wchar_t* val=row_value(&rows[i],vb,4096);
                    if(rows[i].type==R_COLOR){
                        int v=cfg_get_int(rows[i].key,rows[i].dv); unsigned u=(unsigned)v;
                        COLORREF c=RGB((u>>16)&0xFF,(u>>8)&0xFF,u&0xFF);
                        int cw=(int)(22*screen_scale());
                        int cx=rc.right-(int)(14*screen_scale())-cw- (int)(6*screen_scale());
                        HBRUSH cb=CreateSolidBrush(c); RECT cr={cx,yy+(rh-cw)/2,cx+cw,yy+(rh-cw)/2+cw};
                        FillRect(hdc,&cr,cb); DeleteObject(cb);
                        FrameRect(hdc,&cr,(HBRUSH)GetStockObject(GRAY_BRUSH));
                        RECT vr={14,yy,rc.right-(int)(14*screen_scale())-cw-(int)(14*screen_scale()),yy+rh};
                        SelectObject(hdc,fS); SetTextColor(hdc,0x334B74);
                        DrawTextW(hdc,val,-1,&vr,DT_VCENTER|DT_SINGLELINE|DT_RIGHT);
                    } else {
                        int vtop=yy, vbot=yy+rh;
                        if(rows[i].sub){ vtop=yy+(int)(6*screen_scale()); vbot=yy+(int)(26*screen_scale()); }
                        RECT vr={rc.right-(int)(170*screen_scale()),vtop,rc.right-(int)(12*screen_scale()),vbot};
                        SelectObject(hdc,fS); SetTextColor(hdc,0x334B74);
                        DrawTextW(hdc,val,-1,&vr,DT_VCENTER|DT_SINGLELINE|DT_RIGHT);
                    }
                }
            }
        }
        y=yy+rh;
    }
    DeleteObject(fT); DeleteObject(fS); DeleteObject(fH);
    BitBlt(dst,0,0,cw,ch,hdc,0,0,SRCCOPY);
    SelectObject(hdc,ob); DeleteObject(bm); DeleteDC(hdc);
    EndPaint(hSettings,&ps);
}
static void settings_on_command(int i){
    const SRow* r=&rows[i]; wchar_t buf[8192];
    if(r->type==R_SWITCH){
        int nv=!cfg_get_int(r->key,r->dv);
        cfg_set_int(r->key,nv); cfg_save();
        if(!strcmp(r->key,"floatSwitch")){ if(nv) float_show(); else float_window_destroy(); }
        if(!strcmp(r->key,"countdown")){ if(nv){ fl.g=-1; fl.running=0; } present_float(); }
        present_float();
    } else if(r->type==R_CYCLE){
        int v=cfg_get_int(r->key,r->dv)+1; if(v>=r->nc)v=0;
        cfg_set_int(r->key,v); cfg_save(); present_float();
    } else if(r->type==R_COLOR){
        pick_color(hSettings,r->key,r->dv);
    } else if(r->type==R_TEXT){
        const wchar_t* cur=cfg_get_str(r->key,r->ds);
        wchar_t init[8192]; wcscpy(init,cur);
        int ok=0;
        if(!strcmp(r->key,"countdownPattern"))
            ok=FormatDialog(hSettings,L"倒计时格式",1,init,buf,8192);
        else if(!strcmp(r->key,"pattern"))
            ok=FormatDialog(hSettings,L"时间格式",0,init,buf,8192);
        else
            ok=InputBox(hSettings,r->title,init,buf,8192);
        if(ok && buf[0]){ cfg_set_str(r->key,buf); cfg_save(); present_float(); }
    } else if(r->type==R_ACTION){
        int scw=GetSystemMetrics(SM_CXSCREEN),sch=GetSystemMetrics(SM_CYSCREEN);
        g_gx=(scw-g_w)/2; if(g_gx<0)g_gx=0; g_gy=(sch-g_h)/2; if(g_gy<0)g_gy=0;
        cfg_set_int("posX",g_gx); cfg_set_int("posY",g_gy); cfg_save(); present_float();
    }
    InvalidateRect(hSettings,NULL,TRUE);
}
static void pick_color(HWND parent,const char* key,int def){
    CHOOSECOLORW cc; ZeroMemory(&cc,sizeof(cc));
    static COLORREF cust[16]={0};
    cc.lStructSize=sizeof(cc); cc.hwndOwner=parent; cc.lpCustColors=cust;
    int cur=cfg_get_color(key,def); unsigned uu=(unsigned)cur;
    cc.rgbResult=RGB((uu>>16)&0xFF,(uu>>8)&0xFF,uu&0xFF);
    cc.Flags=CC_RGBINIT|CC_FULLOPEN;
    if(ChooseColorW(&cc)){
        int rr=GetRValue(cc.rgbResult),gg=GetGValue(cc.rgbResult),bb=GetBValue(cc.rgbResult);
        cfg_set_color(key,(int)(0xFF000000u|(rr<<16)|(gg<<8)|bb)); cfg_save(); present_float();
    }
    InvalidateRect(hSettings,NULL,TRUE);
}
LRESULT CALLBACK SettingsWndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(g_msgTaskbar && m==g_msgTaskbar){ tray_add(); return 0; }  /* Explorer 重启后重加托盘图标 */
    switch(m){
    case WM_CLOSE: ShowWindow(h,SW_HIDE); return 0;
    case WM_ERASEBKGND: return 1;  /* 双缓冲画布已在 WM_PAINT 全量绘制，禁止背景擦除防闪 */
    case WM_LBUTTONDOWN:{ int y=GET_Y_LPARAM(l); int wi; int i=row_at(y,&wi); if(i>=0) settings_on_command(i); return 0; }
    case WM_SIZE:{ settings_apply_scrollinfo(); InvalidateRect(h,NULL,TRUE); return 0; }
    case WM_VSCROLL:{
        SCROLLINFO si={sizeof(si),SIF_ALL}; GetScrollInfo(h,SB_VERT,&si);
        int pos=si.nPos, step=(int)(40*screen_scale()); if(step<1)step=1;
        int page=si.nPage>step?si.nPage:step;
        switch(LOWORD(w)){
            case SB_TOP: pos=si.nMin; break;
            case SB_BOTTOM: pos=si.nMax; break;
            case SB_LINEUP: pos-=step; break;
            case SB_LINEDOWN: pos+=step; break;
            case SB_PAGEUP: pos-=page; break;
            case SB_PAGEDOWN: pos+=page; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos=si.nTrackPos; break;
        }
        si.fMask=SIF_POS; si.nPos=pos; SetScrollInfo(h,SB_VERT,&si,TRUE);
        GetScrollInfo(h,SB_VERT,&si); g_scroll=si.nPos;
        InvalidateRect(h,NULL,TRUE); return 0;
    }
    case WM_MOUSEWHEEL:{
        /* 累积增量，兼容高分辨率滑鼠/触摸板的<120 增量 */
        wheel_acc+=(short)HIWORD(w);
        /* 滚动步长跟随 Windows 系统设置：行数 × 单行高；0 行=整页 */
        int lines=sys_wheel_lines();
        int lh=RRH*(int)screen_scale(); if(lh<1)lh=1;
        int step= (lines>0)? lines*lh : 0;
        if(step==0){ RECT cr; GetClientRect(h,&cr); step=cr.bottom-cr.top; if(step<1)step=1; }
        while(wheel_acc>=120){ wheel_acc-=120; g_scroll-=step; }
        while(wheel_acc<=-120){ wheel_acc+=120; g_scroll+=step; }
        settings_apply_scrollinfo(); InvalidateRect(h,NULL,TRUE); return 0;
    }
    case WM_MOUSEMOVE:{ int y=GET_Y_LPARAM(l); int wi; int i=row_at(y,&wi); if(i!=g_hover){ g_hover=i; TRACKMOUSEEVENT tm={sizeof(tm),TME_LEAVE,h,0}; TrackMouseEvent(&tm); InvalidateRect(h,NULL,TRUE);} return 0; }
    case WM_MOUSELEAVE:{ g_hover=-1; InvalidateRect(h,NULL,TRUE); return 0; }
    case WM_TRAY:{
        /* 兼容 DOWN/UP 事件：部分系统/触控板只派发 DOWN */
        UINT ev=LOWORD(l);
        if(ev==WM_RBUTTONDOWN||ev==WM_LBUTTONDOWN||ev==WM_RBUTTONUP||ev==WM_LBUTTONUP){
            HMENU mu=CreatePopupMenu();
            AppendMenuW(mu,MF_STRING,5001,L"显示设置");
            AppendMenuW(mu,MF_STRING,5002,L"显示悬浮窗");
            AppendMenuW(mu,MF_STRING,5003,L"隐藏悬浮窗");
            AppendMenuW(mu,MF_SEPARATOR,0,NULL);
            AppendMenuW(mu,MF_STRING,5004,L"退出");
            SetForegroundWindow(h); SetFocus(h);
            POINT pt; GetCursorPos(&pt);
            int cmd=TrackPopupMenu(mu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,h,NULL);
            DestroyMenu(mu);
            PostMessageW(h,WM_NULL,0,0);
            if(cmd==5001) show_settings();
            else if(cmd==5002) float_show();
            else if(cmd==5003) float_hide();
            else if(cmd==5004) PostQuitMessage(0);
        }
        return 0;
    }
    case WM_PAINT: draw_settings_surface(); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

/* =====================================================================
   托盘图标
   ===================================================================== */
#define ID_TRAY 2
static HICON app_icon(int px){
    Bitmap bm(px,px,PixelFormat32bppPARGB);
    Graphics g(&bm); g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0,0,0,0));
    /* 渐变蓝圆盘 */
    LinearGradientBrush lb(PointF(0,0),PointF((REAL)px,(REAL)px),
        Color(255,0x52,0xB7,0xFF),Color(255,0x13,0x6F,0xF0));
    g.FillEllipse(&lb,RectF(0,0,(REAL)px,(REAL)px));
    /* 内圈 */
    SolidBrush in(Color(26,255,255,255));
    g.FillEllipse(&in,RectF((REAL)(px*0.12f),(REAL)(px*0.12f),(REAL)(px*0.76f),(REAL)(px*0.76f)));
    /* 时分针 */
    Pen pen(Color(255,255,255,255),(REAL)(px*0.09f)); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
    float cx=px/2.0f, cy=px/2.0f, r=px*0.30f;
    g.DrawLine(&pen,PointF(cx,cy),PointF(cx,cy-r));                 /* 时针向上 */
    float a2=(float)(3.14159/2.7f);                                 /* 分针斜向 */
    g.DrawLine(&pen,PointF(cx,cy),PointF(cx+r*0.75f* (float)cos(a2), cy-r*0.75f*(float)sin(a2)));
    HICON ic=NULL; bm.GetHICON(&ic); return ic;
}
static HICON tray_icon(void){ return app_icon(16); }
static void tray_add(void){
    NOTIFYICONDATAW n; ZeroMemory(&n,sizeof(n));
    n.cbSize=sizeof(n); n.hWnd=hSettings; n.uID=ID_TRAY;
    n.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    n.uCallbackMessage=WM_TRAY;
    wcscpy(n.szTip,L"时间窗，恢复到悬浮窗");
    HICON ic=tray_icon();
    if(!ic) return;                       /* 图标生成失败则跳过，避免 NIM_ADD 无效 */
    n.hIcon=ic;
    Shell_NotifyIconW(NIM_ADD,&n);
    DestroyIcon(ic);
}
static void tray_rm(void){
    NOTIFYICONDATAW n; ZeroMemory(&n,sizeof(n));
    n.cbSize=sizeof(n); n.hWnd=hSettings; n.uID=ID_TRAY;
    Shell_NotifyIconW(NIM_DELETE,&n);
}

/* =====================================================================
   主入口
   ===================================================================== */
int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int){
    hInst=hi;
    {   /* 启用 DPI 感知 */
        typedef BOOL (WINAPI *SPDA)(void);
        HMODULE u2=LoadLibraryW(L"user32.dll");
        if(u2){ SPDA spda=(SPDA)GetProcAddress(u2,"SetProcessDPIAware"); if(spda) spda(); FreeLibrary(u2); }
    }
    {   /* 激活 Common Controls v6 视觉样式 */
        INITCOMMONCONTROLSEX icex={sizeof(icex),ICC_WIN95_CLASSES|ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icex);
    }

    gdi_start();
    cfg_load();
    /* 全局 UI 字体：与主窗口分区标题字号一致（13 号雅黑），原生控件默认是系统点阵字体 */
    g_uiFont=CreateFontW(-(int)(13*screen_scale()),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei");
    g_gx=cfg_get_int("posX",-1); g_gy=cfg_get_int("posY",-1);
    g_msgTaskbar=RegisterWindowMessageW(L"TaskbarCreated");   /* Explorer 重启重建托盘图标 */
    if(g_gx<0||g_gy<0){ g_gx=(GetSystemMetrics(SM_CXSCREEN)-200)/2; g_gy=GetSystemMetrics(SM_CYSCREEN)/3; }

    WNDCLASSEXW wc; ZeroMemory(&wc,sizeof(wc));
    wc.cbSize=sizeof(wc);
    wc.hInstance=hInst; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hIcon=app_icon(32);      /* 大图标：任务栏 / Alt+Tab */
    wc.hIconSm=app_icon(16);    /* 小图标：标题栏 / 通知区，缺失会导致部分位置无图标 */
    wc.lpfnWndProc=FloatWndProc; wc.lpszClassName=L"TimeWindowFloat"; RegisterClassExW(&wc);
    wc.lpfnWndProc=SettingsWndProc; wc.lpszClassName=L"TimeWindowSettings"; wc.hbrBackground=NULL; RegisterClassExW(&wc);
    wc.lpfnWndProc=InputWndProc; wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"TimeWindowInput"; RegisterClassExW(&wc);
    wc.lpfnWndProc=FmtWndProc; wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"TimeWindowFmt"; RegisterClassExW(&wc);

    /* 托盘依赖设置窗口作为接收窗口，先建隐藏设置窗口 */
    RECT wr; wr.left=0; wr.top=0; wr.right=(int)(430*screen_scale()); wr.bottom=(int)(700*screen_scale());
    AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW,FALSE,0);
    hSettings=CreateWindowExW(0,L"TimeWindowSettings",L"时间窗 设置",
        WS_OVERLAPPEDWINDOW|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,wr.right-wr.left,wr.bottom-wr.top,NULL,NULL,hInst,NULL);
    tray_add();
    ShowWindow(hSettings,SW_SHOW);
    /* 确保初始滚动位置为 0，避免内容偏移 */
    g_scroll=0;
    settings_apply_scrollinfo();
    if(cfg_float_enabled()) float_show();

    MSG msg; while(GetMessageW(&msg,NULL,0,0)>0){ TranslateMessage(&msg); DispatchMessageW(&msg); }

    tray_rm();
    float_window_destroy();
    gdi_stop();
    return 0;
}