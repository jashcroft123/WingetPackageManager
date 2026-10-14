#include "tui_core.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <process.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#define INITIAL (WM_APP+2)
#define LOG_LIMIT (16u*1024u*1024u)
#define DONE (WM_APP+1)
enum  {
  REMOTE=100,INSTALLED,UPDATES,QUERY,GO,REFRESH,ACTION,EXPORT,IMPORT,ALL,CLEAR
};
typedef struct Job  {
  CoreCommand* commands;
  size_t count;
  WCHAR* key;
  WCHAR* preview;
  DWORD generation;
  int kind;
  CoreResult result;
  volatile LONG progress;
  BOOL cacheFailed; BOOL truncated; size_t succeeded,failedCommands,attempted;
}
Job;
static HWND win,grid,query,status,logbox,action;
static HFONT font;
static UINT dpi=96;
static int view=CORE_INSTALLED;
static CorePackages packages;
static size_t* rows;
static size_t rowCount;
static HANDLE thread;
static Job* active;
static Job* pending;
static volatile LONG cancelRead;
static DWORD generation;
static WCHAR* currentKey;
static WCHAR* operationLog;
static BOOL failed;
static BOOL lastTimeout;
static int columnWidth=-1;
static ULONGLONG started;
static void* alloc(size_t n) {
  return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n);
}
static int sc(int n) {
  return MulDiv(n,(int)dpi,96);
}
static WCHAR* getText(HWND h) {
  int n=GetWindowTextLengthW(h);
  WCHAR* s=alloc(((size_t)n+1)*sizeof(WCHAR));
  if(s)GetWindowTextW(h,s,n+1);
  return s;
}
static void freeJob(Job* j) {
  size_t i;
  if(!j)return;
  for(i=0;j->commands&&i<j->count;i++)core_command_free(&j->commands[i]);
  core_free(j->commands);
  core_free(j->key);
  core_free(j->preview);
  core_result_free(&j->result);
  core_free(j);
}
static void chrome(void) {
  WCHAR s[128];
  int n=ListView_GetSelectedCount(grid);
  swprintf_s(s,128,L"%zu packages; %d selected%s",rowCount,n,lastTimeout?L"; timed out; an installer may still run":failed?L"; last operation failed":L"");
  if(!active)SetWindowTextW(status,s);
  SetWindowTextW(action,view==CORE_REMOTE?L"Install selected":view==CORE_UPDATES?L"Update selected":L"Remove selected");
  EnableWindow(action,!active&&n);
  EnableWindow(GetDlgItem(win,EXPORT),!active);
  EnableWindow(GetDlgItem(win,IMPORT),!active);
}
static BOOL contains(const WCHAR* s,const WCHAR* q) {
  size_t n=wcslen(q);
  for(;*s;s++)if(!_wcsnicmp(s,q,n))return TRUE;
  return !n;
}
static void filter(void) {
  size_t i;
  WCHAR* q=getText(query);
  core_free(rows);
  rows=alloc((packages.count?packages.count:1)*sizeof(size_t));
  rowCount=0;
  if(rows)for(i=0;i<packages.count;i++)if(view==CORE_REMOTE||contains(packages.items[i].name,q?q:L"")||contains(packages.items[i].id,q?q:L""))rows[rowCount++]=i;
  core_free(q);
  ListView_SetItemState(grid,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
  ListView_SetItemCountEx(grid,(int)rowCount,LVSICF_NOSCROLL);
  InvalidateRect(grid,NULL,FALSE);
  chrome();
}
static unsigned __stdcall worker(void* p) {
  Job* j=p;
  size_t i,capacity=0;
  CoreBytes log= {
    0
  };
  for(i=0;i<j->count;i++) {
    CoreResult r;
    InterlockedExchange(&j->progress,(LONG)i+1);
    /* core_run uses InterlockedCompareExchange exclusively; analyzer treats the external pointer argument as a plain access. */
#pragma warning(suppress: 28112)
    r=core_run(&j->commands[i],j->kind==1?3600:120,j->kind==1?NULL:&cancelRead);
    if(j->kind!=0) {
      WCHAR header[512];
      WCHAR* id=j->commands[i].count>2?j->commands[i].args[2]:L"";
      const WCHAR* source=L"default";
      UINT a;
      for(a=0;a+1<j->commands[i].count;a++)if(!wcscmp(j->commands[i].args[a],L"--source"))source=j->commands[i].args[a+1];
      char narrow[1600];
      int hn;
      size_t take,need;
      char* next;
      _snwprintf_s(header,512,_TRUNCATE,L"\r\nCommand %zu/%zu: %s %s [source: %s]; exit %lu%s%s\r\n",i+1,j->count,j->commands[i].args[0],id,source,r.code,r.timedOut?L"; timed out":L"",r.error?r.error:L"");
      hn=WideCharToMultiByte(CP_UTF8,0,header,-1,narrow,sizeof(narrow),NULL,NULL);
      if(hn>0)hn--;
      else hn=0;
      take=r.output.length;
      if((size_t)hn>LOG_LIMIT-log.length){hn=0;j->truncated=TRUE;}
      if(take>LOG_LIMIT-log.length-(size_t)hn){take=LOG_LIMIT-log.length-(size_t)hn;j->truncated=TRUE;}
      need=log.length+(size_t)hn+take;
      next=log.data;
      if(need+1>capacity) {
        size_t nextCapacity=capacity?capacity*2:4096;
        if(nextCapacity<need+1)nextCapacity=need+1;
        if(nextCapacity>LOG_LIMIT+1)nextCapacity=LOG_LIMIT+1;
        next=log.data?HeapReAlloc(GetProcessHeap(),0,log.data,nextCapacity):alloc(nextCapacity);
        if(next)capacity=nextCapacity;
      }
      if(next) {
        if(hn)memcpy(next+log.length,narrow,(size_t)hn);
        if(take&&r.output.data)memcpy(next+log.length+(size_t)hn,r.output.data,take);
        log.data=next;
        log.length=need;
        next[need]=0;
      }
      else  {
        r.code=ERROR_NOT_ENOUGH_MEMORY;
        r.error=L"Operation log allocation failed";
      }
    }
    j->attempted++;
    if(r.code||r.error||r.timedOut||r.canceled)j->failedCommands++;else j->succeeded++;
    if(j->result.code&&!r.code)r.code=j->result.code;
    if(j->result.error&&!r.error)r.error=j->result.error;
    core_result_free(&j->result);
    j->result=r;
    if(r.canceled||r.timedOut||r.error)break;
  }
  if(j->kind!=0) {
    core_free(j->result.output.data);
    j->result.output=log;
  }
  if(j->kind==1)core_cache_invalidate();
  if(j->kind==0&&core_list_success(&j->result)&&!InterlockedCompareExchange(&cancelRead,0,0))j->cacheFailed=!core_cache_write(j->key,&j->result.output);
  /* The UI retains ownership until DONE or shutdown joins this worker. */  return PostMessageW(win,DONE,0,(LPARAM)j)?0:1;
}
static void start(Job* j) {
  if(active) {
    if(j->kind==0) {
      freeJob(pending);
      pending=j;
      if(active->kind==0)InterlockedExchange(&cancelRead,1);
    }
    else freeJob(j);
    return;
  }
  InterlockedExchange(&cancelRead,0);
  active=j;
  started=GetTickCount64();
  SetWindowTextW(status,L"Starting command...");
  thread=(HANDLE)_beginthreadex(NULL,0,worker,j,0,NULL);
  if(!thread) {
    active=NULL;
    freeJob(j);
    chrome();
    SetWindowTextW(status,L"Cannot start worker");
    return;
  }
  chrome();
}
static void load(BOOL force) {
  WCHAR* q=getText(query);
  CoreCached cache= {
    0
  };
  Job* j;
  int id;
  generation++;
  for(id=REMOTE;id<=UPDATES;id++)SendDlgItemMessageW(win,id,BM_SETCHECK,id-REMOTE==view?BST_CHECKED:BST_UNCHECKED,0);
  core_free(currentKey);
  currentKey=core_listing_key(view,q?q:L"");
  freeJob(pending);
  pending=NULL;
  if(active&&active->kind==0)InterlockedExchange(&cancelRead,1);
  core_packages_free(&packages);
  if(view==CORE_REMOTE&&(!q||!*q)) {
    filter();
    core_free(q);
    return;
  }
  core_cache_read(currentKey,&cache);
  if(cache.exists)core_parse(&cache.output,&packages);
  filter();
  if(force||!cache.exists||cache.age<0||cache.age>(view==CORE_REMOTE?86400:view==CORE_UPDATES?300:600)) {
    j=alloc(sizeof(*j));
    if(j) {
      j->commands=alloc(sizeof(CoreCommand));
      j->count=1;
      j->key=core_copy(currentKey);
      j->generation=generation;
      if(j->commands&&core_listing_command(j->commands,view,q?q:L""))start(j);
      else freeJob(j);
    }
  }
  core_free(cache.output.data);
  core_free(q);
}
static WCHAR* quoted(const WCHAR* value) {
  size_t n=wcslen(value),slashes=0,k;
  WCHAR* out=alloc((2*n+3)*sizeof(WCHAR));
  WCHAR* p=out;
  if(!out)return NULL;
  *p++=L'"';
  for(;*value;value++) {
    if(*value==L'\\') {
      slashes++;
      continue;
    }
    k=slashes*(*value==L'"'?2:1);
    while(k--)*p++=L'\\';
    if(*value==L'"')*p++=L'\\';
    *p++=*value;
    slashes=0;
  }
  slashes*=2;
  while(slashes--)*p++=L'\\';
  *p++=L'"';
  *p=0;
  return out;
}
static WCHAR* commandText(Job* j) {
  size_t i,k,n=256+(j->preview?wcslen(j->preview):0);
  WCHAR* s;
  for(i=0;i<j->count;i++)for(k=0;k<j->commands[i].count;k++)n+=2*wcslen(j->commands[i].args[k])+4;
  s=alloc(n*sizeof(WCHAR));
  if(!s)return NULL;
  wcscpy_s(s,n,L"Run these exact commands?\r\n\r\n");
  if(j->preview)wcscat_s(s,n,j->preview);
  for(i=0;i<j->count;i++) {
    wcscat_s(s,n,L"winget");
    for(k=0;k<j->commands[i].count;k++) {
      WCHAR* q=quoted(j->commands[i].args[k]);
      if(!q) {
        core_free(s);
        return NULL;
      }
      wcscat_s(s,n,L" ");
      wcscat_s(s,n,q);
      core_free(q);
    }
    wcscat_s(s,n,L"\r\n");
  }
  return s;
}
static void operate(void) {
  Job* j=alloc(sizeof(*j));
  int r=-1;
  size_t i=0;
  WCHAR* message;
  size_t previewSize=256;
  if(!j)return;
  j->kind=1;
  j->count=(size_t)ListView_GetSelectedCount(grid);
  j->commands=alloc(j->count*sizeof(CoreCommand));
  while((r=ListView_GetNextItem(grid,r,LVNI_SELECTED))!=-1)if(i++<12) {
    CorePackage* p=&packages.items[rows[r]];
    previewSize+=wcslen(p->name)+wcslen(p->id)+wcslen(p->source)+32;
  }
  r=-1;
  i=0;
  j->preview=alloc(previewSize*sizeof(WCHAR));
  if(!j->commands||!j->preview) {
    freeJob(j);
    return;
  }
  while((r=ListView_GetNextItem(grid,r,LVNI_SELECTED))!=-1) {
    CorePackage* p=&packages.items[rows[r]];
    if(i<12) {
      wcscat_s(j->preview,previewSize,p->name);
      wcscat_s(j->preview,previewSize,L" (");
      wcscat_s(j->preview,previewSize,p->id);
      wcscat_s(j->preview,previewSize,L") [");
      wcscat_s(j->preview,previewSize,p->source);
      wcscat_s(j->preview,previewSize,L"]\r\n");
    }
    if(wcsstr(p->id,L"...")||wcschr(p->id,0x2026)||!core_package_command(&j->commands[i++],p,view)) {
      freeJob(j);
      MessageBoxW(win,L"Incomplete package ID or allocation failure.",L"Cannot continue",MB_OK);
      return;
    }
  }
  if(j->count>12) {
    WCHAR remaining[64];
    swprintf_s(remaining,64,L"... and %zu more.\r\n",j->count-12);
    wcscat_s(j->preview,previewSize,remaining);
  }
  wcscat_s(j->preview,previewSize,L"\r\n");
  message=commandText(j);
  if(message&&MessageBoxW(win,message,L"Confirm package operation",MB_OKCANCEL|MB_ICONQUESTION)==IDOK)start(j);
  else freeJob(j);
  core_free(message);
}
static void fileOperation(BOOL save) {
  WCHAR* path=alloc(32768*sizeof(WCHAR));
  OPENFILENAMEW of= {
    0
  };
  Job* j;
  WCHAR* message;
  const WCHAR* args[10];
  UINT n=0;
  if(!path)return;
  wcscpy_s(path,32768,L"installed-packages.json");
  of.lStructSize=sizeof(of);
  of.hwndOwner=win;
  of.lpstrFilter=L"WinGet JSON\0*.json\0\0";
  of.lpstrFile=path;
  of.nMaxFile=32768;
  of.lpstrDefExt=L"json";
  of.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
  if(!(save?GetSaveFileNameW(&of):GetOpenFileNameW(&of))) {
    core_free(path);
    return;
  }
  args[n++]=save?L"export":L"import";
  args[n++]=save?L"--output":L"--import-file";
  args[n++]=path;
  args[n++]=save?L"--include-versions":L"--ignore-versions";
  if(!save)args[n++]=L"--accept-package-agreements";
  args[n++]=L"--accept-source-agreements";
  args[n++]=L"--disable-interactivity";
  j=alloc(sizeof(*j));
  if(j) {
    j->kind=save?2:1;
    j->count=1;
    j->commands=alloc(sizeof(CoreCommand));
    if(j->commands&&core_command_create(j->commands,args,n)) {
      message=commandText(j);
      if(message&&MessageBoxW(win,message,L"Confirm setup operation",MB_OKCANCEL)==IDOK)start(j);
      else freeJob(j);
      core_free(message);
    }
    else freeJob(j);
  }
  core_free(path);
}
static HWND control(const WCHAR* cls,const WCHAR* label,DWORD style,int id) {
  HWND h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,win,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
  SendMessageW(h,WM_SETFONT,(WPARAM)font,0);
  return h;
}
static BOOL CALLBACK applyFont(HWND h,LPARAM f) {
  SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);
  return TRUE;
}
static void setFont(UINT d) {
  NONCLIENTMETRICSW m= {
    0
  };
  HFONT next;
  dpi=d;
  m.cbSize=sizeof(m);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(m),&m,0,dpi);
  next=CreateFontIndirectW(&m.lfMessageFont);
  EnumChildWindows(win,applyFont,(LPARAM)next);
  if(font)DeleteObject(font);
  font=next;
  columnWidth=-1;
}
static HDWP layoutBatch;
static void place(HWND h,int x,int y,int w,int ht) {
  if(layoutBatch)layoutBatch=DeferWindowPos(layoutBatch,h,NULL,x,y,w>0?w:1,ht>0?ht:1,SWP_NOZORDER|SWP_NOACTIVATE);
  else MoveWindow(h,x,y,w>0?w:1,ht>0?ht:1,TRUE);
}
static void layout(void) {
  RECT r;
  int w,h,b=sc(98),g=sc(6),m=sc(10),ht=sc(28),y,i;
  GetClientRect(win,&r);
  layoutBatch=BeginDeferWindowPos(14);
  w=r.right;
  h=r.bottom;
  for(i=0;i<3;i++)place(GetDlgItem(win,REMOTE+i),m+i*(b+g),m,b,ht);
  place(GetDlgItem(win,EXPORT),w-m-2*b-g,m,b,ht);
  place(GetDlgItem(win,IMPORT),w-m-b,m,b,ht);
  y=m+ht+g;
  place(query,m,y,w-2*m-2*b-2*g,ht);
  place(GetDlgItem(win,GO),w-m-2*b-g,y,b,ht);
  place(GetDlgItem(win,REFRESH),w-m-b,y,b,ht);
  y+=ht+g;
  place(GetDlgItem(win,ALL),m,y,b,ht);
  place(GetDlgItem(win,CLEAR),m+b+g,y,b,ht);
  place(action,m+2*(b+g),y,sc(150),ht);
  y+=ht+g;
  place(grid,m,y,w-2*m,h-y-sc(130));
  place(status,m,h-sc(120),w-2*m,sc(24));
  place(logbox,m,h-sc(94),w-2*m,sc(84));
  if(layoutBatch){EndDeferWindowPos(layoutBatch);layoutBatch=NULL;}
  if(columnWidth!=w-2*m-sc(24)) {
    columnWidth=w-2*m-sc(24);
    for(i=0;i<5;i++)ListView_SetColumnWidth(grid,i,columnWidth*(i<2?30:i==4?14:13)/100);
  }
}
static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
  switch(msg) {
    case INITIAL:load(FALSE);
    return 0;
    case WM_CREATE: {
      const WCHAR* names[]= {
        L"Package",L"Package ID",L"Version",L"Available",L"Source"
      };
      int i;
      win=h;
      setFont(GetDpiForWindow(h));
      control(L"BUTTON",L"Remote",BS_AUTORADIOBUTTON|BS_PUSHLIKE|WS_TABSTOP|WS_GROUP,REMOTE);
      control(L"BUTTON",L"Installed",BS_AUTORADIOBUTTON|BS_PUSHLIKE,INSTALLED);
      control(L"BUTTON",L"Updates",BS_AUTORADIOBUTTON|BS_PUSHLIKE,UPDATES);
      control(L"BUTTON",L"Export setup",WS_TABSTOP|WS_GROUP,EXPORT);
      control(L"BUTTON",L"Import setup",WS_TABSTOP,IMPORT);
      query=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,QUERY);
      control(L"BUTTON",L"Search",WS_TABSTOP,GO);
      control(L"BUTTON",L"Refresh",WS_TABSTOP,REFRESH);
      control(L"BUTTON",L"Select all",WS_TABSTOP,ALL);
      control(L"BUTTON",L"Clear",WS_TABSTOP,CLEAR);
      action=control(L"BUTTON",L"Remove selected",WS_TABSTOP,ACTION);
      grid=control(WC_LISTVIEWW,L"",WS_BORDER|LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS|WS_TABSTOP,3);
      ListView_SetExtendedListViewStyle(grid,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
      status=control(L"STATIC",L"Ready",0,2);
      logbox=control(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_TABSTOP,4);
      SendMessageW(logbox,EM_SETLIMITTEXT,LOG_LIMIT+1024,0);
      for(i=0;i<5;i++) {
        LVCOLUMNW c= {
          0
        };
        c.mask=LVCF_TEXT;
        c.pszText=(WCHAR*)names[i];
        ListView_InsertColumn(grid,i,&c);
      }
      SetTimer(h,1,500,NULL);
      layout();
      return 0;
    }
    case WM_SIZE:if(grid&&wp!=SIZE_MINIMIZED)layout();
    return 0;
    case WM_DPICHANGED: {
      RECT* r=(RECT*)lp;
      setFont(HIWORD(wp));
      SetWindowPos(h,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);
      layout();
      return 0;
    }
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize.x=sc(680);
    ((MINMAXINFO*)lp)->ptMinTrackSize.y=sc(440);
    return 0;
    case WM_TIMER:if(active&&thread) {
      DWORD exitCode=STILL_ACTIVE;
      if(GetExitCodeThread(thread,&exitCode)&&exitCode==1) {
        SendMessageW(win,DONE,0,(LPARAM)active);
        return 0;
      }
    }
    if(active) {
      WCHAR s[160];
      swprintf_s(s,160,L"%s %ld/%zu; %llus ...",active->kind==0?L"Reading packages":L"Running command",InterlockedCompareExchange(&active->progress,0,0),active->count,(GetTickCount64()-started)/1000);
      SetWindowTextW(status,s);
    }
    return 0;
    case WM_COMMAND: {
      int id=LOWORD(wp);
      if(id==QUERY&&HIWORD(wp)==EN_CHANGE) {
        if(view!=CORE_REMOTE)filter();
        return 0;
      }
      if(id>=REMOTE&&id<=UPDATES) {
        view=id-REMOTE;
        SetWindowTextW(query,L"");
        load(FALSE);
      }
      else if(id==GO||id==REFRESH)load(id==REFRESH);
      else if(id==ALL||id==CLEAR) {
        ListView_SetItemState(grid,-1,id==ALL?LVIS_SELECTED:0,LVIS_SELECTED);
        chrome();
      }
      else if(!active&&id==ACTION)operate();
      else if(!active&&(id==EXPORT||id==IMPORT))fileOperation(id==EXPORT);
      return 0;
    }
    case WM_NOTIFY: {
      NMHDR* n=(NMHDR*)lp;
      if(n->hwndFrom==grid) {
        if(n->code==LVN_ITEMCHANGED||n->code==LVN_ODSTATECHANGED)chrome();
        if(n->code==LVN_GETDISPINFOW) {
          NMLVDISPINFOW* d=(NMLVDISPINFOW*)lp;
          if(d->item.iItem>=0&&(size_t)d->item.iItem<rowCount&&d->item.iSubItem>=0&&d->item.iSubItem<5) {
            CorePackage* p=&packages.items[rows[d->item.iItem]];
            WCHAR* v[]= {
              p->name,p->id,p->version,p->available,p->source
            };
            d->item.pszText=v[d->item.iSubItem];
          }
        }
      }
      return 0;
    }
    case DONE: {
      Job* j=(Job*)lp;
      BOOL mutation=j->kind==1;
      WCHAR* output;
      WaitForSingleObject(thread,INFINITE);
      CloseHandle(thread);
      thread=NULL;
      active=NULL;
      chrome();
      if(j->kind==0) {
        if(j->generation==generation&&!j->result.canceled) {
          if(core_list_success(&j->result)) {
            core_packages_free(&packages);
            core_parse(&j->result.output,&packages);
            filter();
            if(j->cacheFailed)SetWindowTextW(status,lastTimeout?L"Results loaded; cache write failed; an installer may still run":L"Results loaded; cache write failed");
          }
          else {
            WCHAR detail[384];
            swprintf_s(detail,384,L"Read failed: %s (exit %lu); cached results retained%s",j->result.error?j->result.error:j->result.timedOut?L"timed out":L"WinGet returned an error",j->result.code,lastTimeout?L"; an installer may still run":L"");
            SetWindowTextW(status,detail);
            output=core_wide(j->result.output.data,j->result.output.length);
            SetWindowTextW(logbox,L"--- Latest read failure ---\r\n");
            SendMessageW(logbox,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
            SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)detail);
            SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)L"\r\n");
            SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)(output?output:L""));
            SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)L"\r\n--- Previous operation ---\r\n");
            SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)(operationLog?operationLog:L""));
            core_free(output);
          }
        }
      }
      else {
        output=core_wide(j->result.output.data,j->result.output.length);
        core_free(operationLog);
        operationLog=output;
        failed=j->result.code!=0||j->result.timedOut||j->result.error!=NULL;
        lastTimeout=j->result.timedOut;
        WCHAR summary[256];
        swprintf_s(summary,256,L"%zu succeeded; %zu failed; %zu not attempted%s\r\n\r\n",j->succeeded,j->failedCommands,j->count-j->attempted,j->truncated?L"; output truncated at 16 MiB":L"");
        SetWindowTextW(logbox,summary);
        SendMessageW(logbox,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
        SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)(output?output:L""));
        if(j->result.error) {
          SendMessageW(logbox,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
          SendMessageW(logbox,EM_REPLACESEL,0,(LPARAM)j->result.error);
        }
      }
      if(j->kind!=0)chrome();
      freeJob(j);
      if(mutation)load(TRUE);
      else if(pending) {
        Job* next=pending;
        pending=NULL;
        start(next);
      }
      return 0;
    }
    case WM_CLOSE:if(active&&active->kind==1) {
      MessageBoxW(h,L"Wait for package changes to finish.",L"Working",MB_OK);
      return 0;
    }
    InterlockedExchange(&cancelRead,1);
    if(thread) {
      WaitForSingleObject(thread,INFINITE);
      CloseHandle(thread);
      thread=NULL;
    }
    freeJob(active);
    active=NULL;
    freeJob(pending);
    pending=NULL;
    DestroyWindow(h);
    return 0;
    case WM_DESTROY:PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h,msg,wp,lp);
}
int WINAPI wWinMain(_In_ HINSTANCE instance,_In_opt_ HINSTANCE previous,_In_ PWSTR commandLine,_In_ int show) {
  WNDCLASSW c= {
    0
  };
  INITCOMMONCONTROLSEX cc= {
    sizeof(cc),ICC_LISTVIEW_CLASSES
  };
  MSG msg;
  int argc,i;
  BOOL portable=FALSE;
#ifdef WINGETSWIFT_DIAGNOSTICS
  BOOL selfTest=FALSE;
#endif
  WCHAR** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  UNREFERENCED_PARAMETER(previous);
  UNREFERENCED_PARAMETER(commandLine);
  for(i=1;argv&&i<argc;i++) {
    if(!wcscmp(argv[i],L"--portable"))portable=TRUE;
#ifdef WINGETSWIFT_DIAGNOSTICS
    if(!wcscmp(argv[i],L"--self-test"))selfTest=TRUE;
#endif
  }
  LocalFree(argv);
#ifdef WINGETSWIFT_DIAGNOSTICS
  if(selfTest)return core_tests();
#endif
  core_init_cache(portable);
  InitCommonControlsEx(&cc);
  c.lpfnWndProc=proc;
  c.hInstance=instance;
  c.lpszClassName=L"WingetSwiftGuiC";
  c.hCursor=LoadCursorW(NULL,IDC_ARROW);
  c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
  RegisterClassW(&c);
  dpi=GetDpiForSystem();
  win=CreateWindowW(c.lpszClassName,L"WingetSwift C",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,sc(980),sc(680),NULL,NULL,instance,NULL);
  if(!win)return 1;
  ShowWindow(win,show);
  UpdateWindow(win);
  PostMessageW(win,INITIAL,0,0);
  while(GetMessageW(&msg,NULL,0,0)>0) {
    if(msg.message==WM_KEYDOWN&&msg.hwnd==grid&&msg.wParam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)) {
      ListView_SetItemState(grid,-1,LVIS_SELECTED,LVIS_SELECTED);
      chrome();
      continue;
    }
    if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&msg.hwnd==query) {
      load(FALSE);
      continue;
    }
    if(!IsDialogMessageW(win,&msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  core_packages_free(&packages);
  core_free(rows);
  core_free(currentKey);
  core_free(operationLog);
  if(font)DeleteObject(font);
  return 0;
}
