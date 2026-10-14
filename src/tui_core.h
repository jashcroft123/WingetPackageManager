#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stddef.h>

typedef struct { WCHAR* name; WCHAR* id; WCHAR* version; WCHAR* available; WCHAR* source; } CorePackage;
typedef struct { CorePackage* items; size_t count; } CorePackages;
typedef struct { char* data; size_t length; } CoreBytes;
typedef struct { BOOL exists; LONGLONG age; CoreBytes output; } CoreCached;
typedef struct { WCHAR** args; UINT count; } CoreCommand;
typedef struct { DWORD code; BOOL timedOut,canceled; const WCHAR* error; CoreBytes output; } CoreResult;
enum { CORE_REMOTE=0, CORE_INSTALLED=1, CORE_UPDATES=2 };

// All owning pointers returned by the core use HeapAlloc(GetProcessHeap()).
void core_free(void* value);
WCHAR* core_copy(const WCHAR* value);
WCHAR* core_wide(const char* bytes,size_t length);
BOOL core_init_cache(BOOL portable);
WCHAR* core_listing_key(int view,const WCHAR* query);
BOOL core_cache_read(const WCHAR* key,CoreCached* result);
BOOL core_cache_write(const WCHAR* key,const CoreBytes* output);
void core_cache_invalidate(void);
BOOL core_parse(const CoreBytes* output,CorePackages* packages);
void core_packages_free(CorePackages* packages);
BOOL core_command_create(CoreCommand* command,const WCHAR* const* args,UINT count);
BOOL core_package_command(CoreCommand* command,const CorePackage* package,int view);
BOOL core_listing_command(CoreCommand* command,int view,const WCHAR* query);
void core_command_free(CoreCommand* command);
CoreResult core_run(const CoreCommand* command,DWORD timeoutSeconds,volatile LONG* cancel);
void core_result_free(CoreResult* result);
BOOL core_list_success(const CoreResult* result);
int core_cells(WCHAR value);
#ifdef WINGETSWIFT_DIAGNOSTICS
int core_tests(void);
#endif
