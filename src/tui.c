#include "tui_core.h"
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { JOB_LIST, JOB_CHANGE, JOB_EXPORT };
enum { INPUT_NORMAL, INPUT_SEARCH, INPUT_EXPORT, INPUT_IMPORT, INPUT_CONFIRM };
typedef struct {
    int kind, view;
    DWORD generation;
    CoreCommand* commands;
    size_t count;
    WCHAR* key;
    CoreResult result;
    BOOL cacheFailed, truncated;
    volatile LONG progress;
} Job;
typedef struct {
    HANDLE input, original, screen, thread, wake;
    DWORD inputMode;
    WCHAR oldTitle[1024];
    WORD color;
    BOOL consoleReady, logOpen, quit;
    int view, inputKind;
    DWORD generation;
    WCHAR query[1024], status[512], warning[128], typed[32768];
    size_t inputAt, cursor, top, anchor, page, logTop, confirmTop;
    CorePackages packages;
    size_t* rows;
    BYTE* selected;
    size_t rowCount;
    CHAR_INFO* pixels;
    size_t pixelCount;
    WCHAR* log;
    WCHAR* operationLog;
    Job* active;
    Job* pending;
    Job* confirmed;
    volatile LONG cancel;
    ULONGLONG started;
} App;
static App app;
static volatile LONG controlQuit;
static void draw(void);
static void load(BOOL force);

static void* alloc(size_t count, size_t size) {
    if (!count || size > SIZE_MAX / count) return NULL;
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size);
}
static void status(const WCHAR* value) {
    _snwprintf_s(app.status, _countof(app.status), _TRUNCATE, L"%ls", value);
}
static void jobFree(Job* job) {
    if (!job) return;
    for (size_t i = 0; i < job->count; ++i) core_command_free(&job->commands[i]);
    core_free(job->commands); core_free(job->key); core_result_free(&job->result); core_free(job);
}
static Job* jobCreate(int kind, int view, size_t count) {
    Job* job = alloc(1, sizeof(*job));
    if (!job) return NULL;
    job->commands = alloc(count, sizeof(*job->commands));
    if (!job->commands) { core_free(job); return NULL; }
    job->kind = kind; job->view = view; job->count = count; job->generation = app.generation;
    return job;
}
static BOOL bytesAppend(CoreBytes* bytes, const char* data, size_t length, BOOL* truncated) {
    const size_t limit = 16 * 1024 * 1024;
    if (length > limit - bytes->length) { length = limit - bytes->length; *truncated = TRUE; }
    if (!length) return TRUE;
    char* next = bytes->data ? HeapReAlloc(GetProcessHeap(), 0, bytes->data, bytes->length + length + 1)
                             : HeapAlloc(GetProcessHeap(), 0, bytes->length + length + 1);
    if (!next) return FALSE;
    memcpy(next + bytes->length, data, length); bytes->data = next; bytes->length += length; next[bytes->length] = 0;
    return TRUE;
}
static BOOL logWide(CoreBytes* bytes, const WCHAR* message, BOOL* truncated) {
    int length=WideCharToMultiByte(CP_UTF8,0,message,-1,NULL,0,NULL,NULL);
    char* converted=alloc((size_t)length,1);if(!converted)return FALSE;
    BOOL ok=WideCharToMultiByte(CP_UTF8,0,message,-1,converted,length,NULL,NULL)!=0 && bytesAppend(bytes,converted,(size_t)length-1,truncated);
    core_free(converted);return ok;
}
static unsigned __stdcall runJob(void* context) {
    Job* job = context;
    if (job->kind == JOB_LIST) {
        job->result = core_run(&job->commands[0], 120, &app.cancel);
        if (core_list_success(&job->result) && !job->result.canceled)
            job->cacheFailed = !core_cache_write(job->key, &job->result.output);
    } else {
        size_t success = 0, attempted = 0;
        for (size_t i = 0; i < job->count; ++i) {
            InterlockedExchange(&job->progress, (LONG)i + 1);
            CoreCommand* command=&job->commands[i];WCHAR heading[512];const WCHAR* source=L"";
            for(UINT a=0;a+1<command->count;++a)if(!wcscmp(command->args[a],L"--source"))source=command->args[a+1];
            _snwprintf_s(heading,_countof(heading),_TRUNCATE,L"\r\n--- %ls %ls (source: %ls) ---\r\n",command->args[0],command->args[2],source);
            if(!logWide(&job->result.output,heading,&job->truncated)){job->result.error=L"Cannot retain command output.";job->result.code=ERROR_NOT_ENOUGH_MEMORY;break;}
            CoreResult result = core_run(&job->commands[i], job->kind == JOB_CHANGE ? 3600 : 120,
                                         job->kind == JOB_CHANGE ? NULL : &app.cancel);
            ++attempted;
            if (!result.code && !result.timedOut && !result.error) ++success;
            else if (!job->result.code) job->result.code = result.code ? result.code : ERROR_GEN_FAILURE;
            if (!bytesAppend(&job->result.output, result.output.data, result.output.length, &job->truncated)) {
                job->result.code = ERROR_NOT_ENOUGH_MEMORY; job->result.error = L"Cannot retain command output.";
            }
            if (result.error) {
                job->result.error=result.error;
                logWide(&job->result.output,result.error,&job->truncated);
            }
            char exitLine[64];int exitLength=_snprintf_s(exitLine,sizeof(exitLine),_TRUNCATE,"\r\nExit: %lu\r\n",result.code);
            if(exitLength>0)bytesAppend(&job->result.output,exitLine,(size_t)exitLength,&job->truncated);
            bytesAppend(&job->result.output, "\r\n", 2, &job->truncated);
            job->result.timedOut |= result.timedOut; job->result.canceled |= result.canceled;
            core_result_free(&result);
            if (job->result.timedOut || job->result.canceled || job->result.error) break;
        }
        char summary[160];
        int n = _snprintf_s(summary, sizeof(summary), _TRUNCATE, "\r\n%zu succeeded, %zu failed, %zu not attempted.\r\n",
                            success, attempted - success, job->count - attempted);
        if (n > 0) bytesAppend(&job->result.output, summary, (size_t)n, &job->truncated);
        if (job->kind == JOB_CHANGE) core_cache_invalidate();
    }
    return 0;
}
static void start(Job* job) {
    if (app.active) {
        jobFree(app.pending); app.pending = job;
        if (app.active->kind == JOB_LIST) InterlockedExchange(&app.cancel, 1);
        return;
    }
    InterlockedExchange(&app.cancel, 0); app.started = GetTickCount64(); app.active = job;
    app.thread = (HANDLE)_beginthreadex(NULL, 0, runJob, job, 0, NULL);
    if (!app.thread) { app.active = NULL; jobFree(job); status(L"Cannot start background worker."); }
}
static BOOL contains(const WCHAR* text, const WCHAR* query) {
    size_t length = wcslen(query), remaining = wcslen(text);
    if (!length) return TRUE;
    for (; remaining >= length; ++text, --remaining) {
        if (CompareStringOrdinal(text, (int)length, query, (int)length, TRUE) == CSTR_EQUAL) return TRUE;
    }
    return FALSE;
}
static void filter(void) {
    core_free(app.rows); core_free(app.selected);
    app.rows = alloc(app.packages.count, sizeof(*app.rows)); app.selected = alloc(app.packages.count, 1);
    app.rowCount = 0; app.cursor = app.top = app.anchor = 0;
    if (app.packages.count && (!app.rows || !app.selected)) { status(L"Not enough memory for package rows."); return; }
    for (size_t i = 0; i < app.packages.count; ++i) {
        CorePackage* p = &app.packages.items[i];
        if (app.view == CORE_REMOTE || contains(p->name, app.query) || contains(p->id, app.query)) app.rows[app.rowCount++] = i;
    }
}
static void replacePackages(CorePackages* packages) {
    core_packages_free(&app.packages); app.packages = *packages; memset(packages, 0, sizeof(*packages)); filter();
}
static void load(BOOL force) {
    WCHAR* key = core_listing_key(app.view, app.query);
    if (!key) { status(L"Cannot prepare listing key."); return; }
    if (!force && app.active && app.active->kind == JOB_LIST && !app.cancel &&
        app.active->view == app.view && !wcscmp(app.active->key, key)) { core_free(key); return; }
    ++app.generation; jobFree(app.pending); app.pending = NULL;
    if (app.active && app.active->kind == JOB_LIST) InterlockedExchange(&app.cancel, 1);
    if (app.view == CORE_REMOTE && !*app.query) {
        CorePackages empty = {0}; replacePackages(&empty); core_free(key); status(L"Press / to search the remote catalog."); return;
    }
    CoreCached cached = {0}; core_cache_read(key, &cached);
    CorePackages packages = {0};
    if (cached.exists) {
        if (core_parse(&cached.output, &packages)) replacePackages(&packages);
        else { core_packages_free(&packages); replacePackages(&packages); status(L"Cannot parse cached packages; no actions available."); core_free(cached.output.data); core_free(key); return; }
    } else replacePackages(&packages);
    WCHAR message[128]; _snwprintf_s(message, _countof(message), _TRUNCATE, L"Cached: %zu packages", app.rowCount); status(message);
    int ttl = app.view == CORE_REMOTE ? 86400 : app.view == CORE_UPDATES ? 300 : 600;
    BOOL fresh = cached.exists && cached.age >= 0 && cached.age <= ttl;
    core_free(cached.output.data);
    if (!force && fresh) { core_free(key); return; }
    Job* job = jobCreate(JOB_LIST, app.view, 1);
    if (!job) { core_free(key); status(L"Cannot allocate listing."); return; }
    job->generation = app.generation; job->key = key;
    if (!core_listing_command(&job->commands[0], app.view, app.query)) { jobFree(job); status(L"Cannot prepare WinGet arguments."); return; }
    start(job);
}
static void complete(void) {
    Job* job = app.active; CloseHandle(app.thread); app.thread = NULL; app.active = NULL;
    BOOL relevant = job->kind != JOB_LIST || job->generation == app.generation;
    if (relevant && !job->result.canceled) {
        WCHAR* output=core_wide(job->result.output.data,job->result.output.length);
        if(job->result.error&&!job->result.output.length){core_free(output);output=core_copy(job->result.error);}
        core_free(app.log);app.log=NULL;
        if(job->kind==JOB_LIST)app.log=output;
        else {core_free(app.operationLog);app.operationLog=output;}
        app.logTop=0;
        if (job->kind == JOB_LIST && core_list_success(&job->result)) {
            CorePackages packages = {0};
            if (core_parse(&job->result.output, &packages)) { replacePackages(&packages);
                WCHAR message[128]; _snwprintf_s(message, _countof(message), _TRUNCATE, L"Live: %zu packages%ls", app.rowCount, job->cacheFailed ? L"; cache write failed" : L"; cached"); status(message); }
            else { core_packages_free(&packages); status(L"Cannot parse WinGet results."); }
        } else status(job->result.timedOut ? L"Timed out. An installer may still run; inspect output before retrying." :
                      job->result.code || job->result.error ? L"Failed; press L for output." : L"Completed; press L for output.");
        if (job->truncated) status(L"Output truncated at 16 MiB; press L for retained output.");
    }
    BOOL reload = job->kind == JOB_CHANGE;
    if (reload) _snwprintf_s(app.warning, _countof(app.warning), _TRUNCATE, L"%ls",
        job->result.timedOut ? L"Last change timed out; installer may still run" :
        job->result.code || job->result.error ? L"Last change failed; press L for output" : L"");
    jobFree(job);
    if (reload) load(TRUE);
    else if (app.pending) { Job* next = app.pending; app.pending = NULL; start(next); }
}
static size_t selectionCount(void) {
    size_t count = 0; for (size_t i = 0; i < app.rowCount; ++i) count += !!app.selected[i]; return count;
}
static void prepareChange(void) {
    if (app.active) { status(L"Wait for background work before changing packages."); return; }
    size_t count = selectionCount(); if (!count) { status(L"Select packages with Space or Ctrl+A first."); return; }
    Job* job = jobCreate(JOB_CHANGE, app.view, count); if (!job) { status(L"Not enough memory for selected commands."); return; }
    size_t at = 0;
    for (size_t i = 0; i < app.rowCount; ++i) if (app.selected[i]) {
        CorePackage* p = &app.packages.items[app.rows[i]];
        if (wcschr(p->id, 0x2026) || wcsstr(p->id, L"...") || !core_package_command(&job->commands[at++], p, app.view)) {
            jobFree(job); status(L"Truncated package ID or command allocation failure; narrow the search."); return;
        }
    }
    app.confirmed = job; app.inputKind = INPUT_CONFIRM; app.confirmTop = 0;
    _snwprintf_s(app.typed, _countof(app.typed), _TRUNCATE, L"%ls %zu selected packages? Agreements accepted. Y/N",
                 app.view == CORE_REMOTE ? L"Install" : app.view == CORE_UPDATES ? L"Update" : L"Remove", count);
}
static void beginInput(int kind) {
    if (app.active && kind != INPUT_SEARCH) { status(L"Wait for background work first."); return; }
    app.inputKind = kind;
    _snwprintf_s(app.typed, _countof(app.typed), _TRUNCATE, L"%ls", kind == INPUT_SEARCH ? app.query : L"installed-packages.json");
    app.inputAt = wcslen(app.typed);
}
static void acceptInput(void) {
    int kind = app.inputKind; app.inputKind = INPUT_NORMAL;
    if (kind == INPUT_SEARCH) {
        size_t first=0,last=wcslen(app.typed);while(first<last&&app.typed[first]==L' ')++first;while(last>first&&app.typed[last-1]==L' ')--last;
        if(last-first>=_countof(app.query)){status(L"Search is limited to 1023 characters.");app.inputKind=INPUT_SEARCH;return;}
        memcpy(app.query,app.typed+first,(last-first)*sizeof(WCHAR));app.query[last-first]=0;
        if (app.view == CORE_REMOTE) load(FALSE); else filter(); return; }
    if (!*app.typed) { status(L"No file path supplied."); return; }
    BOOL importing = kind == INPUT_IMPORT;
    const WCHAR* args[] = { importing ? L"import" : L"export", importing ? L"--import-file" : L"--output", app.typed,
                           importing ? L"--ignore-versions" : L"--include-versions", L"--accept-source-agreements",
                           importing ? L"--accept-package-agreements" : L"--disable-interactivity", L"--disable-interactivity" };
    Job* job = jobCreate(importing ? JOB_CHANGE : JOB_EXPORT, app.view, 1);
    if (!job || !core_command_create(&job->commands[0], args, importing ? 7 : 6)) { jobFree(job); status(L"Cannot prepare file command."); return; }
    if (importing) { app.confirmed = job; app.inputKind = INPUT_CONFIRM; app.confirmTop = 0;
        _snwprintf_s(app.typed, _countof(app.typed), _TRUNCATE, L"Import latest versions from JSON? Agreements accepted. Y/N"); }
    else start(job);
}
static void moveCursor(LONGLONG delta, BOOL range) {
    if (!app.rowCount) return;
    LONGLONG next = (LONGLONG)app.cursor + delta;
    if (next < 0) next = 0; if ((ULONGLONG)next >= app.rowCount) next = (LONGLONG)app.rowCount - 1;
    app.cursor = (size_t)next;
    if (range) { memset(app.selected, 0, app.rowCount); size_t a = app.cursor < app.anchor ? app.cursor : app.anchor;
        size_t b = app.cursor > app.anchor ? app.cursor : app.anchor; for (size_t i = a; i <= b; ++i) app.selected[i] = 1; }
    else app.anchor = app.cursor;
}
static void key(const KEY_EVENT_RECORD* event) {
    if (!event->bKeyDown) return;
    WORD vk = event->wVirtualKeyCode; WCHAR ch = event->uChar.UnicodeChar;
    BOOL ctrl = !!(event->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED));
    BOOL shift = !!(event->dwControlKeyState & SHIFT_PRESSED);
    if (app.inputKind == INPUT_CONFIRM) {
        if (ch == L'y' || ch == L'Y') { Job* job = app.confirmed; app.confirmed = NULL; app.inputKind = INPUT_NORMAL; start(job); }
        else if (ch == L'n' || ch == L'N' || vk == VK_ESCAPE) { jobFree(app.confirmed); app.confirmed = NULL; app.inputKind = INPUT_NORMAL; }
        else if (vk == VK_DOWN || vk == VK_NEXT) { size_t step = vk == VK_NEXT ? app.page : 1; if (app.confirmTop + step < app.confirmed->count) app.confirmTop += step; }
        else if (vk == VK_UP || vk == VK_PRIOR) { size_t step = vk == VK_PRIOR ? app.page : 1; app.confirmTop = app.confirmTop > step ? app.confirmTop - step : 0; }
        return;
    }
    if (app.inputKind != INPUT_NORMAL) {
        size_t length = wcslen(app.typed);
        if (vk == VK_ESCAPE) app.inputKind = INPUT_NORMAL;
        else if (vk == VK_RETURN) acceptInput();
        else if (ctrl && vk == L'A') { app.typed[0] = 0; app.inputAt = 0; }
        else if (vk == VK_LEFT && app.inputAt) --app.inputAt;
        else if (vk == VK_RIGHT && app.inputAt < length) ++app.inputAt;
        else if (vk == VK_HOME) app.inputAt = 0;
        else if (vk == VK_END) app.inputAt = length;
        else if (vk == VK_BACK && app.inputAt) { memmove(app.typed + app.inputAt - 1, app.typed + app.inputAt, (length - app.inputAt + 1) * sizeof(WCHAR)); --app.inputAt; }
        else if (vk == VK_DELETE && app.inputAt < length) memmove(app.typed + app.inputAt, app.typed + app.inputAt + 1, (length - app.inputAt) * sizeof(WCHAR));
        else if (ch >= 32 && length < _countof(app.typed) - 1) { memmove(app.typed + app.inputAt + 1, app.typed + app.inputAt, (length - app.inputAt + 1) * sizeof(WCHAR)); app.typed[app.inputAt++] = ch; }
        return;
    }
    if (ch == L'q' || ch == L'Q' || vk == VK_ESCAPE) {
        if (app.active && app.active->kind == JOB_CHANGE) status(L"Package changes are running; wait before quitting."); else app.quit = TRUE;
    } else if (ch >= L'1' && ch <= L'3') { app.view = ch - L'1'; app.query[0] = 0; app.logOpen = FALSE; load(FALSE); }
    else if (ch == L'/') beginInput(INPUT_SEARCH);
    else if (ch == L'r' || ch == L'R') load(TRUE);
    else if (ch == L'e' || ch == L'E') beginInput(INPUT_EXPORT);
    else if (ch == L'i' || ch == L'I') beginInput(INPUT_IMPORT);
    else if (ch == L'l' || ch == L'L') app.logOpen = !app.logOpen;
    else if (app.logOpen) { if (vk == VK_DOWN) ++app.logTop; else if (vk == VK_UP && app.logTop) --app.logTop;
        else if (vk == VK_NEXT) app.logTop += app.page; else if (vk == VK_PRIOR) app.logTop = app.logTop > app.page ? app.logTop - app.page : 0; else if (vk == VK_HOME) app.logTop = 0; }
    else if (ctrl && vk == L'A') { if (app.rowCount) memset(app.selected, 1, app.rowCount); }
    else if (vk == VK_UP) moveCursor(-1, shift);
    else if (vk == VK_DOWN) moveCursor(1, shift);
    else if (vk == VK_PRIOR) moveCursor(-(LONGLONG)app.page, shift);
    else if (vk == VK_NEXT) moveCursor((LONGLONG)app.page, shift);
    else if (vk == VK_HOME) moveCursor(-(LONGLONG)app.rowCount, shift);
    else if (vk == VK_END) moveCursor((LONGLONG)app.rowCount, shift);
    else if (vk == VK_SPACE && app.rowCount) { app.selected[app.cursor] ^= 1; app.anchor = app.cursor; }
    else if (vk == VK_RETURN) prepareChange();
}
static void put(int width, int height, int x, int y, int limit, const WCHAR* value, WORD color) {
    if (y < 0 || y >= height || x < 0 || x >= width || !value) return;
    int end = x + limit; if (end > width) end = width;
    for (; *value && x < end; ++value) { int cells = core_cells(*value); if (x + cells > end) break;
        app.pixels[(size_t)y * width + x].Char.UnicodeChar = *value;
        app.pixels[(size_t)y * width + x].Attributes = color | (cells == 2 ? COMMON_LVB_LEADING_BYTE : 0);
        if (cells == 2) { app.pixels[(size_t)y * width + x + 1].Char.UnicodeChar = *value; app.pixels[(size_t)y * width + x + 1].Attributes = color | COMMON_LVB_TRAILING_BYTE; }
        x += cells;
    }
}
static void draw(void) {
    CONSOLE_SCREEN_BUFFER_INFO info; if (!GetConsoleScreenBufferInfo(app.screen, &info)) return;
    int width = info.srWindow.Right - info.srWindow.Left + 1, height = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (width > 512) width = 512; if (height > 200) height = 200;
    if (width <= 0 || height <= 0) return;
    size_t count = (size_t)width * height;
    if (count != app.pixelCount) { CHAR_INFO* pixels = alloc(count, sizeof(*pixels)); if (!pixels) return; core_free(app.pixels); app.pixels = pixels; app.pixelCount = count; }
    for (size_t i = 0; i < count; ++i) { app.pixels[i].Char.UnicodeChar = L' '; app.pixels[i].Attributes = app.color; }
    WORD highlight = (WORD)(((app.color & 15) << 4) | ((app.color >> 4) & 15));
    WCHAR line[512];
    if (width < 40 || height < 8) { put(width, height, 0, 0, width, L"Enlarge terminal to at least 40 x 8.", app.color); goto present; }
    put(width, height, 0, 0, width, L"Winget Swift C TUI   1 Remote | 2 Installed | 3 Updates", highlight);
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"%ls  %zu packages; %zu selected  Query: %ls", app.view == CORE_REMOTE ? L"Remote" : app.view == CORE_UPDATES ? L"Updates" : L"Installed", app.rowCount, selectionCount(), app.query);
    put(width, height, 0, 1, width, line, app.color);
    app.page = height > 8 ? (size_t)height - 7 : 1;
    if (app.cursor < app.top) app.top = app.cursor;
    if (app.cursor >= app.top + app.page) app.top = app.cursor - app.page + 1;
    if (app.inputKind == INPUT_CONFIRM && app.confirmed) {
        put(width, height, 0, 2, width, L"Exact commands to approve (Up/Down/PgUp/PgDn review; Y applies; N cancels)", highlight);
        for (size_t i = 0; i < app.page && app.confirmTop + i < app.confirmed->count; ++i) {
            CoreCommand* command = &app.confirmed->commands[app.confirmTop + i];
            const WCHAR* source = L"";
            for (UINT a = 0; a + 1 < command->count; ++a) if (!wcscmp(command->args[a], L"--source")) source = command->args[a+1];
            _snwprintf_s(line, _countof(line), _TRUNCATE, L"%zu. %ls %ls%ls%ls%ls", app.confirmTop + i + 1,
                         command->args[0], command->args[2], *source ? L" (source: " : L"", source, *source ? L")" : L"");
            put(width, height, 0, (int)i + 3, width, line, app.color);
        }
    } else if (app.logOpen) {
        const WCHAR* segments[3]={app.operationLog?app.operationLog:L"",app.operationLog&&app.log?L"\r\n--- Latest package listing ---\r\n":L"",app.log?app.log:app.operationLog?L"":L"No completed command output yet."};size_t row=0,displayed=0;
        for(int segment=0;segment<3&&displayed<app.page;++segment){const WCHAR* p=segments[segment];
        while (*p && displayed < app.page) { const WCHAR* end = wcschr(p, L'\n'); size_t length = end ? (size_t)(end - p) : wcslen(p);
            if (row++ >= app.logTop) { size_t n = length < _countof(line) - 1 ? length : _countof(line) - 1; memcpy(line, p, n * sizeof(WCHAR)); line[n] = 0; if (n && line[n-1] == L'\r') line[n-1] = 0; put(width, height, 0, (int)displayed++ + 3, width, line, app.color); }
            if (!end) break; p = end + 1;
        }}
        put(width, height, 0, 2, width, L"Output (L returns to packages; Up/Down/PgUp/PgDn scroll)", highlight);
    } else {
        int nameWidth = width * 35 / 100, idWidth = width * 37 / 100, versionWidth = width - nameWidth - idWidth - 6;
        put(width, height, 0, 2, width, L"    PACKAGE", highlight); put(width, height, nameWidth + 4, 2, idWidth, L"PACKAGE ID", highlight); put(width, height, nameWidth + idWidth + 5, 2, versionWidth, L"VERSION / UPDATE", highlight);
        for (size_t i = 0; i < app.page && app.top + i < app.rowCount; ++i) {
            size_t row = app.top + i; CorePackage* p = &app.packages.items[app.rows[row]]; WORD color = row == app.cursor ? highlight : app.color;
            for (int x = 0; x < width; ++x) app.pixels[(i + 3) * width + x].Attributes = color;
            put(width, height, 0, (int)i + 3, 4, app.selected[row] ? L"[x] " : L"[ ] ", color);
            put(width, height, 4, (int)i + 3, nameWidth - 1, p->name, color); put(width, height, nameWidth + 4, (int)i + 3, idWidth, p->id, color);
            _snwprintf_s(line, _countof(line), _TRUNCATE, L"%ls%ls%ls", p->version, *p->available ? L" > " : L"", p->available);
            put(width, height, nameWidth + idWidth + 5, (int)i + 3, versionWidth, line, color);
        }
    }
    if (app.active) { const WCHAR* title = app.active->kind == JOB_LIST ? app.active->view == CORE_REMOTE ? L"Searching" : app.active->view == CORE_UPDATES ? L"Checking updates" : L"Loading installed" : app.active->kind == JOB_EXPORT ? L"Exporting" : L"Changing packages";
        _snwprintf_s(line, _countof(line), _TRUNCATE, L"%ls %ld/%zu  %llu s", title, InterlockedCompareExchange(&app.active->progress, 0, 0), app.active->count, (GetTickCount64() - app.started) / 1000); }
    else _snwprintf_s(line, _countof(line), _TRUNCATE, L"%ls%ls%ls", app.status, *app.warning ? L"; " : L"", app.warning);
    put(width, height, 0, height - 3, width, line, highlight);
    put(width, height, 0, height - 2, width, L"/ search  Space select  Shift+arrows range  Ctrl+A all  Enter apply  R refresh", app.color);
    put(width, height, 0, height - 1, width, L"E export  I import  L output  Q quit | Paths: Ctrl+A clears; Enter accepts; Esc cancels", app.color);
    if (app.inputKind != INPUT_NORMAL) { const WCHAR* label = app.inputKind == INPUT_SEARCH ? L"Search/filter: " : app.inputKind == INPUT_EXPORT ? L"Export path: " : app.inputKind == INPUT_IMPORT ? L"Import path: " : L"Confirm: ";
        for (int x = 0; x < width; ++x) { app.pixels[(size_t)(height-1)*width+x].Char.UnicodeChar=L' '; app.pixels[(size_t)(height-1)*width+x].Attributes=highlight; }
        int labelWidth=(int)wcslen(label), available=width-labelWidth, caret=0;size_t first=0;
        if(app.inputKind!=INPUT_CONFIRM){for(size_t i=0;i<app.inputAt;++i)caret+=core_cells(app.typed[i]);while(caret>=available&&first<app.inputAt)caret-=core_cells(app.typed[first++]);}
        put(width, height, 0, height - 1, width, label, highlight); put(width, height, labelWidth, height - 1, available, app.typed+first, highlight);
        CONSOLE_CURSOR_INFO cursor={1,app.inputKind!=INPUT_CONFIRM};SetConsoleCursorInfo(app.screen,&cursor);
        if(cursor.bVisible){COORD position={(SHORT)(info.srWindow.Left+labelWidth+caret),(SHORT)(info.srWindow.Top+height-1)};SetConsoleCursorPosition(app.screen,position);}
    }else{CONSOLE_CURSOR_INFO cursor={1,FALSE};SetConsoleCursorInfo(app.screen,&cursor);}
present:;
    SMALL_RECT target = info.srWindow; target.Right = (SHORT)(target.Left + width - 1); target.Bottom = (SHORT)(target.Top + height - 1);
    COORD size = {(SHORT)width, (SHORT)height}, origin = {0,0}; WriteConsoleOutputW(app.screen, app.pixels, size, origin, &target);
}
static BOOL WINAPI control(DWORD kind) { if (kind == CTRL_C_EVENT || kind == CTRL_BREAK_EVENT) { InterlockedExchange(&controlQuit, 1); SetEvent(app.wake); return TRUE; } return FALSE; }
static BOOL consoleBegin(void) {
    app.input = GetStdHandle(STD_INPUT_HANDLE); app.original = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleMode(app.input, &app.inputMode) || !GetConsoleScreenBufferInfo(app.original, &info)) return FALSE;
    app.wake = CreateEventW(NULL, FALSE, FALSE, NULL); if (!app.wake) return FALSE;
    app.color = info.wAttributes; GetConsoleTitleW(app.oldTitle, _countof(app.oldTitle));
    app.screen = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (app.screen == INVALID_HANDLE_VALUE) return FALSE;
    if (!SetConsoleMode(app.input, (app.inputMode | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT) & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT)) || !SetConsoleActiveScreenBuffer(app.screen)) {
        SetConsoleMode(app.input, app.inputMode); CloseHandle(app.screen); app.screen = INVALID_HANDLE_VALUE; return FALSE;
    }
    CONSOLE_CURSOR_INFO cursor = {1,FALSE}; SetConsoleCursorInfo(app.screen, &cursor); SetConsoleTitleW(L"Winget Swift C TUI"); SetConsoleCtrlHandler(control, TRUE); app.consoleReady = TRUE; return TRUE;
}
static void cleanup(void) {
    if (app.thread) { InterlockedExchange(&app.cancel, 1); WaitForSingleObject(app.thread, INFINITE); CloseHandle(app.thread); }
    jobFree(app.active); jobFree(app.pending); jobFree(app.confirmed); core_packages_free(&app.packages);
    core_free(app.rows); core_free(app.selected); core_free(app.pixels); core_free(app.log);core_free(app.operationLog);
    if (app.consoleReady) { SetConsoleActiveScreenBuffer(app.original); SetConsoleMode(app.input, app.inputMode); SetConsoleTitleW(app.oldTitle); SetConsoleCtrlHandler(control, FALSE); CloseHandle(app.screen); }
    if (app.wake) CloseHandle(app.wake);
}
#ifdef WINGETSWIFT_DIAGNOSTICS
static int uiTests(void) {
    app.view = CORE_INSTALLED;
    app.packages.items = alloc(5, sizeof(CorePackage)); app.packages.count = 5;
    if (!app.packages.items) return 1;
    for (size_t i = 0; i < 5; ++i) { app.packages.items[i].name = core_copy(L"Example"); app.packages.items[i].id = core_copy(L"Example.ID"); app.packages.items[i].version = core_copy(L"1"); app.packages.items[i].available = core_copy(L""); app.packages.items[i].source = core_copy(L"winget"); }
    filter(); int failures = app.rowCount != 5;
    moveCursor(2, TRUE); failures += selectionCount() != 3;
    moveCursor(-1, TRUE); failures += selectionCount() != 2;
    moveCursor(100, FALSE); failures += app.cursor != 4;
    wcscpy_s(app.query, _countof(app.query), L"missing"); filter(); failures += app.rowCount != 0;
    app.query[0] = 0; filter(); failures += app.rowCount != 5;
    CoreBytes bytes = {0}; BOOL truncated = FALSE; failures += !bytesAppend(&bytes,"abc",3,&truncated) || bytes.length != 3 || strcmp(bytes.data,"abc"); core_free(bytes.data);
    cleanup(); return failures;
}
#endif
int wmain(int argc, WCHAR** argv) {
    BOOL portable = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--portable")) portable = TRUE;
        else if (!wcscmp(argv[i], L"--help")) { puts("Winget Swift C TUI: 1/2/3 views, / search, arrows move, Space select, Shift range, Ctrl+A all, Enter apply, R refresh, E export, I import, L output, Q quit. --portable stores cache beside EXE."); return 0; }
#ifdef WINGETSWIFT_DIAGNOSTICS
        else if (!wcscmp(argv[i], L"--self-test")) return core_tests();
        else if (!wcscmp(argv[i], L"--ui-test")) return uiTests();
#endif
    }
    app.view = CORE_INSTALLED;
    if (!core_init_cache(portable) || !consoleBegin()) { fputs("Open Winget Swift C TUI in an interactive Windows console. Cache initialization must succeed.\n",stderr); cleanup(); return 1; }
    status(L"Ready"); draw(); load(FALSE);
    while (!app.quit) {
        if (InterlockedExchange(&controlQuit, 0)) { if (app.active && app.active->kind == JOB_CHANGE) status(L"Wait for package changes before quitting."); else app.quit = TRUE; }
        if (app.quit) break;
        HANDLE handles[3] = {app.input,app.thread ? app.thread : app.wake,app.wake};
        DWORD wait = WaitForMultipleObjects(app.thread ? 3 : 2, handles, FALSE, app.thread ? 500 : INFINITE);
        if (wait == WAIT_OBJECT_0) { INPUT_RECORD records[32]; DWORD count;
            if (!ReadConsoleInputW(app.input, records, _countof(records), &count)) break;
            for (DWORD i = 0; i < count && !app.quit; ++i) if (records[i].EventType == KEY_EVENT) key(&records[i].Event.KeyEvent);
        } else if (app.thread && wait == WAIT_OBJECT_0 + 1) complete();
        else if (wait == WAIT_FAILED) break;
        draw();
    }
    cleanup(); return 0;
}
