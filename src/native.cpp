#include "backend.hpp"
#include <process.h>

// Windows owns the controls, painting, scrollbars and selection behavior.
constexpr UINT DONE=WM_APP+1, PROGRESS=WM_APP+2;
constexpr UINT_PTR busyTimer=1;
enum { COUNTS=10, SELECTALL=210, CLEARSELECTION };
HWND window,grid,query,status,logBox,action,refresh,go,exportButton,importButton;
HFONT font;
UINT dpi=96;
int view=INSTALLED, columnWidth=-1;
bool busy=false,listingActive=false,mutationActive=false,pendingListing=false;
std::atomic_bool readCancel=false;
std::vector<Package> packages,visible;
std::vector<std::wstring> pendingArgs;
std::wstring currentKey,pendingKey,activeKey,operationTitle,mutationWarning;
std::string lastOperation;
steady_clock::time_point operationStarted;

struct WorkerThread {
 HANDLE handle=nullptr;
 void join(){if(handle){WaitForSingleObject(handle,INFINITE);CloseHandle(handle);handle=nullptr;}}
 template<class Work> void launch(Work work){auto context=new Work(std::move(work));handle=(HANDLE)_beginthreadex(nullptr,0,[](void* value)->unsigned{std::unique_ptr<Work> task((Work*)value);(*task)();return 0;},context,0,nullptr);if(!handle){delete context;throw std::runtime_error("Cannot start background worker");}}
 ~WorkerThread(){join();}
} worker;
struct Completed {Result result;bool mutation=false,listing=false,canceled=false;std::wstring key;std::string error;};
int scale(int n){return MulDiv(n,dpi,96);}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
void setTextChanged(HWND h,const std::wstring& s){if(text(h)!=s)SetWindowTextW(h,s.c_str());}
void showBusy(){auto seconds=duration_cast<std::chrono::seconds>(steady_clock::now()-operationStarted).count();setTextChanged(status,operationTitle+L"...  "+wnumber(seconds)+L"s");}
void setStatus(const std::wstring& s){if(busy)showBusy();else SetWindowTextW(status,(s+(mutationWarning.empty()?L"":L"; "+mutationWarning)).c_str());}
void updateChrome(){int count=ListView_GetSelectedCount(grid);setTextChanged(GetDlgItem(window,COUNTS),wnumber(visible.size())+L" packages; "+wnumber(count)+L" selected");setTextChanged(action,view==REMOTE?L"Install selected":view==UPDATES?L"Update selected":L"Remove selected");EnableWindow(action,!busy&&count>0);EnableWindow(GetDlgItem(window,SELECTALL),!visible.empty());EnableWindow(GetDlgItem(window,CLEARSELECTION),count>0);}
void filter(){ListView_SetItemState(grid,-1,0,LVIS_SELECTED|LVIS_FOCUSED);auto q=view==REMOTE?L"":lower(text(query));visible.clear();for(auto& p:packages)if(q.empty()||lower(p.name+L" "+p.id).find(q)!=std::wstring::npos)visible.push_back(p);ListView_SetItemCountEx(grid,(int)visible.size(),LVSICF_NOSCROLL);InvalidateRect(grid,nullptr,FALSE);updateChrome();}
void setBusy(bool value){busy=value;if(value){operationStarted=steady_clock::now();SetTimer(window,busyTimer,500,nullptr);showBusy();SetWindowTextW(refresh,listingActive?L"Checking...":L"Working...");}else{KillTimer(window,busyTimer);SetWindowTextW(refresh,L"Refresh");SetWindowTextW(status,L"Ready");}EnableWindow(exportButton,!busy);EnableWindow(importButton,!busy);updateChrome();}
std::vector<std::wstring> listingArgs(){if(view==REMOTE)return {L"search",L"--query",trim(text(query)),L"--source",L"winget",L"--count",L"1000",L"--accept-source-agreements",L"--disable-interactivity"};if(view==UPDATES)return {L"list",L"--upgrade-available",L"--accept-source-agreements",L"--disable-interactivity"};return {L"list",L"--accept-source-agreements",L"--disable-interactivity"};}
void start(std::vector<std::wstring> args,bool mutation,bool listing,std::wstring key={},std::vector<Command> batch={}){
 if(busy){if(listing){pendingListing=true;pendingArgs=std::move(args);pendingKey=std::move(key);if(listingActive)readCancel=true;}return;}
 worker.join();readCancel=false;mutationActive=mutation;listingActive=listing;activeKey=key;
 operationTitle=mutation?(args[0]==L"upgrade"?L"Updating packages":args[0]==L"uninstall"?L"Removing packages":L"Installing packages"):listing?(key==L"updates"?L"Checking for updates":key==L"installed"?L"Refreshing installed packages":L"Searching packages"):L"Exporting packages";
 setBusy(true);std::wstring command=L"winget";for(auto& a:args)command+=L" "+quote(a);SetWindowTextW(logBox,command.c_str());
 try{worker.launch([args=std::move(args),mutation,listing,key=std::move(key),batch=std::move(batch)]{
  auto c=new Completed;c->mutation=mutation;c->listing=listing;c->key=key;
  try{c->result=batch.empty()?run(args,mutation?3600:120,mutation?nullptr:&readCancel):runBatch(batch,[&](const Command& command){auto message=new std::wstring((command[0]==L"upgrade"?L"Updating ":command[0]==L"uninstall"?L"Removing ":L"Installing ")+wnumber(&command-batch.data()+1)+L"/"+wnumber(batch.size())+L" "+command[2]);if(!PostMessageW(window,PROGRESS,0,(LPARAM)message))delete message;return run(command,3600);});if(mutation)invalidate();if(listing&&listSuccess(c->result)){try{writeCache(key,c->result.output);}catch(const std::exception& e){c->error=std::string("Results loaded; ")+e.what();}}}catch(const std::exception& e){c->error=e.what();}
  c->canceled=listing&&readCancel.load()&&c->result.code==ERROR_CANCELLED;if(!PostMessageW(window,DONE,0,(LPARAM)c))delete c;
 });}catch(const std::exception& e){listingActive=mutationActive=false;setBusy(false);SetWindowTextW(logBox,wide(e.what()).c_str());setStatus(L"Cannot start background work; see output.");}
}
void load(bool force=false){
 for(auto id:{REMOTE,INSTALLED,UPDATES})SendMessageW(GetDlgItem(window,id),BM_SETCHECK,id==view?BST_CHECKED:BST_UNCHECKED,0);
 auto wanted=view==REMOTE?L"search:"+lower(trim(text(query))):view==UPDATES?L"updates":L"installed";
 if(busy&&listingActive&&!force&&!readCancel&&wanted==activeKey){currentKey=wanted;pendingListing=false;pendingArgs.clear();showBusy();return;}
 pendingListing=false;pendingArgs.clear();currentKey.clear();if(listingActive)readCancel=true;
 if(view==REMOTE&&trim(text(query)).empty()){packages.clear();filter();setStatus(L"Search remote packages by name or ID, then press Enter.");return;}
 currentKey=std::move(wanted);auto cached=readCache(currentKey);
 if(cached.exists){packages=parse(cached.output);filter();setStatus(L"Cached: "+wnumber(visible.size())+L" packages");}else{packages.clear();filter();}
 int ttl=view==REMOTE?86400:view==UPDATES?300:600;if(force||!cached.exists||cached.age<0||cached.age>ttl)start(listingArgs(),false,true,currentKey);
}
std::wstring chooseFile(bool save){wchar_t path[32768]=L"installed-packages.json";OPENFILENAMEW of{sizeof(of)};of.hwndOwner=window;of.lpstrFilter=L"WinGet package export (*.json)\0*.json\0\0";of.lpstrFile=path;of.nMaxFile=32768;of.lpstrDefExt=L"json";of.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);if(save?GetSaveFileNameW(&of):GetOpenFileNameW(&of))return path;return {};}
std::vector<Package> selectedPackages(){std::vector<Package> selected;for(int row=-1;(row=ListView_GetNextItem(grid,row,LVNI_SELECTED))!=-1;)if((size_t)row<visible.size())selected.push_back(visible[row]);return selected;}
void operate(){auto selected=selectedPackages();if(selected.empty())return;for(auto& p:selected)if(p.id.find(L'\x2026')!=p.id.npos||p.id.find(L"...")!=p.id.npos){MessageBoxW(window,L"A selected package has a truncated ID. Narrow the search or deselect it.",L"Incomplete package ID",MB_OK|MB_ICONWARNING);return;}
 std::wstring verb=view==REMOTE?L"install":view==UPDATES?L"upgrade":L"uninstall",message=L"Confirm "+verb+L" of "+wnumber(selected.size())+L" package(s):\n\n";for(size_t i=0;i<std::min<size_t>(selected.size(),12);++i)message+=selected[i].name+L" ("+selected[i].id+L")\n";if(selected.size()>12)message+=L"... and "+wnumber(selected.size()-12)+L" more.\n";message+=L"\nPackages run one at a time. Package/source agreements will be accepted.";
 if(MessageBoxW(window,message.c_str(),L"Confirm package operation",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
 std::vector<Command> commands;for(auto& p:selected)commands.push_back(packageCommand(p,view));auto first=commands.front();start(std::move(first),true,false,{},std::move(commands));
}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int id){HWND h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,FALSE);return h;}
void setFont(UINT value){dpi=value;NONCLIENTMETRICSW metrics{sizeof(metrics)};SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi);HFONT next=CreateFontIndirectW(&metrics.lfMessageFont);EnumChildWindows(window,[](HWND h,LPARAM f)->BOOL{SendMessageW(h,WM_SETFONT,(WPARAM)f,FALSE);return TRUE;},(LPARAM)next);if(font)DeleteObject(font);font=next;columnWidth=-1;}
void layout(){if(!grid)return;RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;HDWP batch=BeginDeferWindowPos(15);auto place=[&](HWND child,int x,int y,int width,int height){if(batch)batch=DeferWindowPos(batch,child,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);};int margin=scale(10),gap=scale(6),button=scale(94),height=scale(28),step=height+gap;
 int x=margin;for(auto id:{REMOTE,INSTALLED,UPDATES}){place(GetDlgItem(window,id),x,margin,button,height);x+=button+gap;}
 place(exportButton,w-margin-2*button-gap,margin,button,height);place(importButton,w-margin-button,margin,button,height);
 place(query,margin,margin+step,w-2*margin-2*button-2*gap,height);place(go,w-margin-2*button-gap,margin+step,button,height);place(refresh,w-margin-button,margin+step,button,height);
 place(GetDlgItem(window,SELECTALL),margin,margin+2*step,button,height);place(GetDlgItem(window,CLEARSELECTION),margin+button+gap,margin+2*step,button,height);place(action,margin+2*(button+gap),margin+2*step,scale(132),height);place(GetDlgItem(window,COUNTS),margin+2*(button+gap)+scale(140),margin+2*step+scale(5),w-2*margin-2*(button+gap)-scale(140),scale(22));
 int tableTop=margin+3*step,tableBottom=h-margin-scale(110);place(grid,margin,tableTop,w-2*margin,std::max(scale(60),tableBottom-tableTop));place(status,margin,tableBottom+gap,w-2*margin,scale(22));place(logBox,margin,tableBottom+scale(30),w-2*margin,scale(80));if(batch)EndDeferWindowPos(batch);
 RECT client;GetClientRect(grid,&client);int available=std::max(0,(int)client.right-GetSystemMetricsForDpi(SM_CXVSCROLL,dpi)-scale(4));if(available!=columnWidth){columnWidth=available;int weights[]={29,31,13,13,14},used=0;for(int i=0;i<5;++i){int width=i==4?available-used:available*weights[i]/100;ListView_SetColumnWidth(grid,i,width);used+=width;}}
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM wp,LPARAM lp){switch(m){
 case WM_CREATE:window=h;setFont(GetDpiForWindow(h));control(L"BUTTON",L"Remote",BS_AUTORADIOBUTTON|BS_PUSHLIKE|WS_TABSTOP|WS_GROUP,REMOTE);control(L"BUTTON",L"Installed",BS_AUTORADIOBUTTON|BS_PUSHLIKE,INSTALLED);control(L"BUTTON",L"Updates",BS_AUTORADIOBUTTON|BS_PUSHLIKE,UPDATES);exportButton=control(L"BUTTON",L"Export setup",WS_TABSTOP|WS_GROUP,EXPORT);importButton=control(L"BUTTON",L"Import setup",WS_TABSTOP,IMPORT);query=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SEARCH);SendMessageW(query,EM_SETCUEBANNER,TRUE,(LPARAM)L"Filter packages by name or ID");go=control(L"BUTTON",L"Search",WS_TABSTOP,GO);refresh=control(L"BUTTON",L"Refresh",WS_TABSTOP,REFRESH);control(L"BUTTON",L"Select all",WS_TABSTOP,SELECTALL);control(L"BUTTON",L"Clear",WS_TABSTOP,CLEARSELECTION);action=control(L"BUTTON",L"Remove selected",WS_TABSTOP,ACTION);control(L"STATIC",L"0 packages",0,COUNTS);grid=control(WC_LISTVIEWW,L"",WS_BORDER|LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS|WS_TABSTOP,3);ListView_SetExtendedListViewStyle(grid,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);status=control(L"STATIC",L"Ready",SS_LEFTNOWORDWRAP,2);logBox=control(L"EDIT",L"Operation output appears here.",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_TABSTOP,LOG);{const wchar_t* columns[]={L"Package",L"Package ID",L"Version",L"Available",L"Source"};for(int i=0;i<5;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)columns[i];c.cx=scale(100);ListView_InsertColumn(grid,i,&c);}}layout();updateChrome();return 0;
 case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout();return 0;
 case WM_DPICHANGED:setFont(HIWORD(wp));{auto r=(RECT*)lp;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}layout();return 0;
 case WM_SETTINGCHANGE:if(grid){setFont(GetDpiForWindow(h));layout();InvalidateRect(h,nullptr,TRUE);}return 0;
 case WM_GETMINMAXINFO:{RECT r{0,0,scale(680),scale(400)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);((MINMAXINFO*)lp)->ptMinTrackSize={r.right-r.left,r.bottom-r.top};return 0;}
 case WM_TIMER:if(wp==busyTimer&&busy)showBusy();return 0;
 case PROGRESS:{auto message=(std::wstring*)lp;if(busy){operationTitle=*message;showBusy();}delete message;return 0;}
 case WM_COMMAND:{int id=LOWORD(wp);if(id==SEARCH&&HIWORD(wp)==EN_CHANGE){if(view!=REMOTE)filter();return 0;}if(busy&&(id==ACTION||id==IMPORT||id==EXPORT))return 0;if(id==SELECTALL||id==CLEARSELECTION){ListView_SetItemState(grid,-1,id==SELECTALL?LVIS_SELECTED:0,LVIS_SELECTED);updateChrome();return 0;}if(id==REMOTE||id==INSTALLED||id==UPDATES){view=id;SetWindowTextW(query,L"");SendMessageW(query,EM_SETCUEBANNER,TRUE,(LPARAM)(view==REMOTE?L"Search remote packages (Enter)":L"Filter packages by name or ID"));load();}else if(id==GO)load();else if(id==REFRESH)load(true);else if(id==ACTION)operate();else if(id==EXPORT){auto p=chooseFile(true);if(!p.empty())start({L"export",L"--output",p,L"--include-versions",L"--accept-source-agreements",L"--disable-interactivity"},false,false);}else if(id==IMPORT){auto p=chooseFile(false);if(!p.empty()&&MessageBoxW(window,L"Install latest available versions from this JSON? Review the file first. Package/source agreements will be accepted.",L"Import machine setup",MB_OKCANCEL|MB_ICONQUESTION)==IDOK)start({L"import",L"--import-file",p,L"--ignore-versions",L"--accept-package-agreements",L"--accept-source-agreements",L"--disable-interactivity"},true,false);}return 0;}
 case WM_NOTIFY:{auto n=(NMHDR*)lp;if(n->hwndFrom==grid){if(n->code==LVN_ITEMCHANGED||n->code==LVN_ODSTATECHANGED)updateChrome();if(n->code==LVN_GETDISPINFOW){auto d=(NMLVDISPINFOW*)lp;int i=d->item.iItem;if(i>=0&&(size_t)i<visible.size()&&d->item.iSubItem>=0&&d->item.iSubItem<5){auto& p=visible[i];const std::wstring* values[]={&p.name,&p.id,&p.version,&p.available,&p.source};d->item.pszText=(LPWSTR)values[d->item.iSubItem]->c_str();}}}return 0;}
 case DONE:{auto c=(Completed*)lp;worker.join();setBusy(false);if(c->canceled){delete c;listingActive=mutationActive=false;if(pendingListing){pendingListing=false;auto args=std::move(pendingArgs);auto key=std::move(pendingKey);start(std::move(args),false,true,std::move(key));}else setStatus(currentKey.empty()?L"Search remote packages by name or ID, then press Enter.":L"Cached: "+wnumber(visible.size())+L" packages");return 0;}auto output=c->result.output+(!c->error.empty()?"\r\n"+c->error:"");if(!c->listing)lastOperation=output;SetWindowTextW(logBox,wide((c->listing&&!lastOperation.empty()?lastOperation+"\r\n--- Package refresh ---\r\n":"")+output).c_str());bool relevant=!c->listing||c->key==currentKey,ok=c->error.empty()&&c->result.code==0&&!c->result.timeout;if(c->mutation)mutationWarning=ok?L"":c->result.timeout?L"Last operation timed out; an installer may still run":L"Last package operation failed; see output";if(relevant&&c->listing&&listSuccess(c->result)&&(!c->result.output.empty()||c->error.empty())){packages=parse(c->result.output);filter();setStatus(L"Live: "+wnumber(visible.size())+L" packages"+(!c->error.empty()?L"; cache unavailable":L"; cached for next launch"));}else if(relevant)setStatus(c->result.timeout?L"Timed out; see output. An installer may still be running.":ok?L"Completed; see output for warnings.":L"Failed; see output (exit "+wnumber(c->result.code)+L").");bool reload=c->mutation;if(reload){packages.clear();filter();}delete c;listingActive=mutationActive=false;if(reload)load(true);else if(pendingListing){pendingListing=false;auto args=std::move(pendingArgs);auto key=std::move(pendingKey);start(std::move(args),false,true,std::move(key));}return 0;}
 case WM_CLOSE:if(busy&&mutationActive){MessageBoxW(h,L"Package changes are running. Wait for completion before closing.",L"Working",MB_OK);return 0;}readCancel=true;worker.join();{MSG pending;while(PeekMessageW(&pending,h,PROGRESS,PROGRESS,PM_REMOVE))delete (std::wstring*)pending.lParam;while(PeekMessageW(&pending,h,DONE,DONE,PM_REMOVE))delete (Completed*)pending.lParam;}DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,busyTimer);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,m,wp,lp);}

#ifdef WINGETSWIFT_DIAGNOSTICS
#include "native-tests.hpp"
#endif
int WINAPI wWinMain(_In_ HINSTANCE instance,_In_opt_ HINSTANCE,_In_ PWSTR,_In_ int show){
 int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool portable=false,diagnostic=false;
#ifdef WINGETSWIFT_DIAGNOSTICS
 bool selfTest=false,liveTest=false,uiTest=false,benchmark=false;
#endif
 for(int i=1;i<argc;++i){portable|=wcscmp(argv[i],L"--portable")==0;
#ifdef WINGETSWIFT_DIAGNOSTICS
 selfTest|=wcscmp(argv[i],L"--self-test")==0;liveTest|=wcscmp(argv[i],L"--live-test")==0;uiTest|=wcscmp(argv[i],L"--ui-test")==0;benchmark|=wcscmp(argv[i],L"--benchmark")==0;diagnostic=selfTest||liveTest||uiTest||benchmark;
#endif
 }LocalFree(argv);try{initCache(portable);}catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"Winget Swift",MB_OK|MB_ICONERROR);return 1;}
#ifdef WINGETSWIFT_DIAGNOSTICS
 if(selfTest)return tests();if(liveTest)return nativeLiveTests();
#endif
 INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"WingetSwift";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);dpi=GetDpiForSystem();window=CreateWindowExW(0,wc.lpszClassName,L"Winget Swift",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,scale(1000),scale(680),nullptr,nullptr,instance,nullptr);if(!window)return 1;RECT initial{0,0,scale(900),scale(600)};AdjustWindowRectExForDpi(&initial,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);SetWindowPos(window,nullptr,0,0,initial.right-initial.left,initial.bottom-initial.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(GetDlgItem(window,INSTALLED),BM_SETCHECK,BST_CHECKED,0);ShowWindow(window,show);UpdateWindow(window);
#ifdef WINGETSWIFT_DIAGNOSTICS
 if(uiTest){int result=nativeTests();DestroyWindow(window);DeleteObject(font);return result;}if(benchmark){DestroyWindow(window);DeleteObject(font);return 0;}
#endif
 if(!diagnostic)load();MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&GetFocus()==query){load();continue;}if(msg.message==WM_KEYDOWN&&GetFocus()==grid&&GetKeyState(VK_CONTROL)<0&&msg.wParam==L'A'){ListView_SetItemState(grid,-1,LVIS_SELECTED,LVIS_SELECTED);continue;}if(IsDialogMessageW(window,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}worker.join();DeleteObject(font);return 0;
}
