#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <appmodel.h>
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib,"wintrust.lib")
#pragma comment(lib,"crypt32.lib")
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <functional>
namespace fs = std::filesystem;
using namespace std::chrono;
struct Package { std::wstring name,id,version,available,source; };
// Integer-only formatting avoids pulling generic printf conversion into callers.
template<class Char> std::basic_string<Char> decimal(long long value){Char buffer[21];Char* end=buffer+21;Char* at=end;unsigned long long magnitude=value<0?0ull-(unsigned long long)value:(unsigned long long)value;do{*--at=(Char)('0'+magnitude%10);magnitude/=10;}while(magnitude);if(value<0)*--at=(Char)'-';return {at,end};}
std::string number(long long value){return decimal<char>(value);}
std::wstring wnumber(long long value){wchar_t buffer[21];_i64tow_s(value,buffer,21,10);return buffer;}
std::wstring wide(const std::string& s) { int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0); std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n); return w; }
std::string utf8(const std::wstring& w) { int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr); std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr); return s; }
std::wstring trim(std::wstring s) { auto a=s.find_first_not_of(L" \t\r\n"); if(a==s.npos)return {}; return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1); }
std::wstring lower(std::wstring s) { for(auto& c:s)c=(wchar_t)towlower(c); return s; }
// Windows command-line quoting, including embedded quotes and trailing backslashes.
std::wstring quote(const std::wstring& s) { std::wstring r=L"\""; size_t slashes=0; for(auto c:s) { if(c==L'\\'){++slashes;continue;} r.append(slashes*(c==L'"'?2:1),L'\\'); if(c==L'"')r+=L'\\'; r+=c; slashes=0; } r.append(slashes*2,L'\\'); return r+L'"'; }
int cells(wchar_t c) { return (c>=0x1100 && (c<=0x115f || (c>=0x2e80&&c<=0xa4cf) || (c>=0xac00&&c<=0xd7a3) || (c>=0xf900&&c<=0xfaff) || (c>=0xff01&&c<=0xff60)))?2:1; }
std::wstring slice(const std::wstring& s,int start,int end) { int pos=0; std::wstring r; for(auto c:s) { if(pos>=start&&pos<end)r+=c; pos+=cells(c); } return trim(r); }
std::wstring cleanLine(std::wstring line) {
 while(!line.empty()&&line.back()==L'\r')line.pop_back();
 auto cr=line.find_last_of(L'\r');if(cr!=line.npos)line.erase(0,cr+1);
 std::wstring out;for(size_t i=0;i<line.size();++i){if(line[i]==27&&i+1<line.size()&&line[i+1]==L'['){i+=2;while(i<line.size()&&!(line[i]>=0x40&&line[i]<=0x7e))++i;}else if(line[i]!=0xfeff)out+=line[i];}return out;
}
bool dashed(const std::wstring& s){auto t=trim(s);return t.size()>8&&t.find_first_not_of(L'-')==t.npos;}
std::vector<Package> parse(const std::string& raw) {
 auto input=wide(raw);std::wstring line;std::vector<std::wstring> lines;for(size_t at=0;at<input.size();){auto end=input.find(L'\n',at);if(end==input.npos)end=input.size();auto clean=cleanLine(input.substr(at,end-at));if(!trim(clean).empty())lines.push_back(std::move(clean));at=end+1;}
 std::vector<int> starts; std::vector<std::wstring> names; std::vector<Package> out;
 for(size_t row=0;row<lines.size();++row) {line=lines[row];
  if(dashed(line)&&row>0) {starts.clear();names.clear();int col=0;bool word=false;std::wstring token;for(auto c:lines[row-1]){if(c!=L' '&&c!=L'\t'){if(!word){starts.push_back(col);token.clear();word=true;}token+=c;}else if(word){names.push_back(token);word=false;}col+=cells(c);}if(word)names.push_back(token);continue;}
  if(starts.size()<3||(row+1<lines.size()&&dashed(lines[row+1])))continue;
  Package p;for(size_t i=0;i<starts.size();++i){auto v=slice(line,starts[i],i+1<starts.size()?starts[i+1]:100000);if(i==0)p.name=v;else if(i==1)p.id=v;else if(i==2)p.version=v;else if(names[i]==L"Available")p.available=v;else if(names[i]==L"Source")p.source=v;}
  bool validId=p.id.find_first_of(L" \t")==p.id.npos||p.id.rfind(L"ARP\\",0)==0;
  if(!p.name.empty()&&!p.id.empty()&&validId&&!p.version.empty()&&p.version.find_first_of(L" \t")==p.version.npos)out.push_back(std::move(p));
 } return out;
}
struct Result { DWORD code=0; std::string output; bool timeout=false; };
bool listSuccess(const Result& r){return !r.timeout&&(r.code==0||r.code==0x8a150014);}
bool microsoftSigned(const std::wstring& path){WINTRUST_FILE_INFO file{sizeof(file)};file.pcwszFilePath=path.c_str();WINTRUST_DATA data{sizeof(data)};data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;bool trusted=WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&policy,&data)==ERROR_SUCCESS,identity=false;if(trusted){auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;if(signer&&signer->csCertChain){wchar_t name[128];CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,128);identity=wcscmp(name,L"Microsoft Corporation")==0;}}data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&policy,&data);return trusted&&identity;}
std::wstring wingetPath(){static const std::wstring verified=[](){constexpr auto family=L"Microsoft.DesktopAppInstaller_8wekyb3d8bbwe";UINT32 count=0,length=0;if(GetPackagesByPackageFamily(family,&count,nullptr,&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||!count)throw std::runtime_error("Microsoft App Installer is missing. Install it from Microsoft, then reopen Winget Swift.");std::vector<PWSTR> names(count);std::vector<wchar_t> buffer(length);if(GetPackagesByPackageFamily(family,&count,names.data(),&length,buffer.data())!=ERROR_SUCCESS)throw std::runtime_error("Cannot locate Microsoft's registered App Installer package.");for(UINT32 i=0;i<count;++i){UINT32 size=0;if(GetPackagePathByFullName(names[i],&size,nullptr)!=ERROR_INSUFFICIENT_BUFFER)continue;std::vector<wchar_t> location(size);if(GetPackagePathByFullName(names[i],&size,location.data())!=ERROR_SUCCESS)continue;std::wstring path=std::wstring(location.data())+L"\\winget.exe";if(microsoftSigned(path))return path;}throw std::runtime_error("No Microsoft-signed WinGet executable found in App Installer. Repair App Installer, then reopen Winget Swift.");}();return verified;}
Result run(const std::vector<std::wstring>& args,int seconds,const std::atomic_bool* cancel=nullptr) {
 auto exe=wingetPath();
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE rd=nullptr,wr=nullptr;
 if(!CreatePipe(&rd,&wr,&sa,0))throw std::runtime_error("Cannot create output pipe"); SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
 HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
 std::wstring cmd=quote(exe); for(auto& a:args)cmd+=L" "+quote(a);
 STARTUPINFOW si{sizeof(si)}; si.dwFlags=STARTF_USESTDHANDLES; si.hStdOutput=wr; si.hStdError=wr; si.hStdInput=input; PROCESS_INFORMATION pi{};
 BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi); DWORD launchError=ok?0:GetLastError();CloseHandle(wr); CloseHandle(input);
 if(!ok){CloseHandle(rd);throw std::runtime_error("Could not start WinGet (Windows error "+number(launchError)+")");}
 Result result; auto deadline=steady_clock::now()+std::chrono::seconds(seconds); char buffer[8192];
 for(;;) {if(cancel&&cancel->load()){TerminateProcess(pi.hProcess,ERROR_CANCELLED);break;} if(steady_clock::now()>deadline){result.timeout=true;TerminateProcess(pi.hProcess,ERROR_TIMEOUT);break;} DWORD available=0,read=0; if(!PeekNamedPipe(rd,nullptr,0,nullptr,&available,nullptr))break;
  if(available) { if(ReadFile(rd,buffer,std::min<DWORD>(available,sizeof(buffer)),&read,nullptr)) { if(result.output.size()<16*1024*1024)result.output.append(buffer,read); } continue; }
  if(WaitForSingleObject(pi.hProcess,20)==WAIT_OBJECT_0) { // Exit can race the previous pipe peek: drain once more before leaving.
   while(PeekNamedPipe(rd,nullptr,0,nullptr,&available,nullptr)&&available) { if(!ReadFile(rd,buffer,std::min<DWORD>(available,sizeof(buffer)),&read,nullptr))break; if(result.output.size()<16*1024*1024)result.output.append(buffer,read); } break;
  }
  if(steady_clock::now()>deadline){result.timeout=true;TerminateProcess(pi.hProcess,ERROR_TIMEOUT);break;}
 }
 WaitForSingleObject(pi.hProcess,5000); GetExitCodeProcess(pi.hProcess,&result.code); CloseHandle(rd);CloseHandle(pi.hThread);CloseHandle(pi.hProcess); return result;
}
std::wstring cacheDir;
std::wstring cachePath(const std::wstring& key) {uint64_t h=14695981039346656037ull;for(auto c:utf8(key)){h^=(unsigned char)c;h*=1099511628211ull;}wchar_t hex[17];int at=16;hex[at]=0;do{hex[--at]=L"0123456789abcdef"[h&15];h>>=4;}while(h);return cacheDir+L"\\"+(hex+at)+L".cache";}
struct Cached {std::string output;long long age=0;bool exists=false;};
struct FileHandle{HANDLE value;explicit FileHandle(HANDLE h):value(h){}~FileHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}FileHandle(const FileHandle&)=delete;};
Cached readCache(const std::wstring& key){Cached c;FileHandle file(CreateFileW(cachePath(key).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.value==INVALID_HANDLE_VALUE)return c;LARGE_INTEGER size;if(!GetFileSizeEx(file.value,&size)||size.QuadPart<=0||size.QuadPart>16*1024*1024+8192+100)return c;std::string bytes((size_t)size.QuadPart,0);DWORD read;if(!ReadFile(file.value,bytes.data(),(DWORD)bytes.size(),&read,nullptr)||read!=bytes.size())return c;char* end=nullptr;errno=0;auto stamp=_strtoi64(bytes.c_str(),&end,10);if(errno==ERANGE||stamp<0||end==bytes.c_str()||*end!='\n')return c;c.output=bytes.substr((size_t)(end-bytes.c_str())+1);c.age=duration_cast<std::chrono::seconds>(system_clock::now().time_since_epoch()).count()-stamp;c.exists=true;return c;}
struct CacheEntry{std::wstring path;uint64_t time;};
std::vector<CacheEntry> cacheEntries(){std::vector<CacheEntry> entries;WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW((cacheDir+L"\\*.cache").c_str(),&data);if(find==INVALID_HANDLE_VALUE)return entries;do{if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))entries.push_back({cacheDir+L"\\"+data.cFileName,((uint64_t)data.ftLastWriteTime.dwHighDateTime<<32)|data.ftLastWriteTime.dwLowDateTime});}while(FindNextFileW(find,&data));FindClose(find);return entries;}
void writeCache(const std::wstring& key,const std::string& output){SHCreateDirectoryExW(nullptr,cacheDir.c_str(),nullptr);auto attributes=GetFileAttributesW(cacheDir.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("Cannot create cache directory");auto path=cachePath(key),temporary=path+L"."+wnumber(GetCurrentProcessId())+L".tmp";bool written=false;{FileHandle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.value!=INVALID_HANDLE_VALUE){auto bytes=number(duration_cast<std::chrono::seconds>(system_clock::now().time_since_epoch()).count())+"\n"+output;DWORD count;written=WriteFile(file.value,bytes.data(),(DWORD)bytes.size(),&count,nullptr)&&count==bytes.size();}}if(!written){DeleteFileW(temporary.c_str());throw std::runtime_error("Cache write failed");}if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)){DeleteFileW(temporary.c_str());throw std::runtime_error("Cache replacement failed");}auto files=cacheEntries();if(files.size()>128){std::sort(files.begin(),files.end(),[](const CacheEntry& a,const CacheEntry& b){return a.time<b.time;});for(size_t i=0;i<files.size()-128;++i)DeleteFileW(files[i].path.c_str());}}
void invalidate(){for(auto& entry:cacheEntries())DeleteFileW(entry.path.c_str());}
enum {REMOTE=100,INSTALLED,UPDATES,SEARCH,GO,REFRESH,ACTION,EXPORT,IMPORT,LOG};
void initCache(bool portable){wchar_t path[32768];if(portable){DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error("Cannot find portable cache path");std::wstring executable(path,n);cacheDir=executable.substr(0,executable.find_last_of(L"\\/"))+L"\\cache";}else{DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);if(!n||n>=32768){n=GetTempPathW(32768,path);if(!n||n>=32768)throw std::runtime_error("Cannot find cache path");}cacheDir=std::wstring(path,n)+L"\\WingetSwift\\cache";}}
std::vector<Package> performanceFixture() {std::ifstream file("benchmark-fixture.txt",std::ios::binary);std::string raw((std::istreambuf_iterator<char>(file)),{});if(!raw.empty())return parse(raw);std::vector<Package> fixture;for(int i=1;i<=98;++i)fixture.push_back({L"Example package "+wnumber(i),L"Example.Package"+wnumber(i),L"1.2.3",L"1.3.0",L"winget"});return fixture;}
void signalPerformanceReady(HWND window) {RECT client;GetClientRect(window,&client);std::ofstream state("perf-state-"+number(GetCurrentProcessId())+".txt");state<<(uint64_t)(UINT_PTR)window<<'\n'<<client.right<<'\n'<<client.bottom<<'\n'<<GetDpiForWindow(window)<<'\n';state.close();static HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\WingetSwiftReady"+wnumber(GetCurrentProcessId())).c_str());SetEvent(ready);}
using Command = std::vector<std::wstring>;
template<class Execute> Result runBatch(const std::vector<Command>& commands,const Execute& execute) {
 Result total;size_t succeeded=0,attempted=0;
 for(auto& args:commands) {
  ++attempted;total.output+="\r\n--- winget";for(auto& a:args)total.output+=" "+utf8(quote(a));total.output+=" ---\r\n";
  Result r;try{r=execute(args);}catch(const std::exception& e){r.code=ERROR_GEN_FAILURE;r.output=e.what();}
  total.output+=r.output+"\r\nExit: "+number(r.code)+"\r\n";
  if(r.code==0&&!r.timeout)++succeeded;else if(total.code==0)total.code=r.code?r.code:ERROR_TIMEOUT;
  if(r.timeout){total.timeout=true;break;}
 }
 total.output+="\r\nSummary: "+number(succeeded)+" succeeded, "+number(attempted-succeeded)+" failed, "+number(commands.size()-attempted)+" not attempted.\r\n";
 return total;
}
Command packageCommand(const Package& p,int selectedView) {
 Command args{selectedView==REMOTE?L"install":selectedView==UPDATES?L"upgrade":L"uninstall",L"--id",p.id,L"--exact",L"--accept-source-agreements",L"--disable-interactivity"};if(!p.source.empty()){args.push_back(L"--source");args.push_back(p.source);}else if(selectedView==REMOTE){args.push_back(L"--source");args.push_back(L"winget");}if(selectedView!=INSTALLED)args.push_back(L"--accept-package-agreements");return args;
}
