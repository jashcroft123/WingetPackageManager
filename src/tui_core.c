#include "tui_core.h"
#include <appmodel.h>
#include <limits.h>
#include <shlobj.h>
#include <softpub.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <wintrust.h>

#define LIMIT (16u * 1024u * 1024u)
static WCHAR *cache_dir;
/* The frontend serializes calls on one worker. Cache the verified immutable
   WindowsApps path, and resolve again if App Installer removes that version. */
static WCHAR *verified_exe;
void core_free(void *p) {
  if (p)
    HeapFree(GetProcessHeap(), 0, p);
}
static void *alloc(size_t n) {
  return n ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n) : NULL;
}
static WCHAR *span(const WCHAR *p, size_t n) {
  WCHAR *r;
  if (n > (SIZE_MAX / sizeof(WCHAR)) - 1)
    return NULL;
  r = alloc((n + 1) * sizeof(WCHAR));
  if (r)
    memcpy(r, p, n * sizeof(WCHAR));
  return r;
}
WCHAR *core_copy(const WCHAR *p) {
  return span(p ? p : L"", p ? wcslen(p) : 0);
}
WCHAR *core_wide(const char *p, size_t n) {
  int count;
  WCHAR *r;
  if (n > INT_MAX || (!p && n))
    return NULL;
  if (!n)
    return core_copy(L"");
  count = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, NULL, 0);
  if (!count)
    return NULL;
  r = alloc(((size_t)count + 1) * sizeof(WCHAR));
  if (r && !MultiByteToWideChar(CP_UTF8, 0, p, (int)n, r, count)) {
    core_free(r);
    r = NULL;
  }
  return r;
}
static WCHAR *join(const WCHAR *a, const WCHAR *b) {
  size_t n = wcslen(a), m = wcslen(b);
  WCHAR *r;
  if (n > SIZE_MAX - m - 1 || n + m > SIZE_MAX / sizeof(WCHAR) - 1)
    return NULL;
  r = alloc((n + m + 1) * sizeof(WCHAR));
  if (r) {
    memcpy(r, a, n * sizeof(WCHAR));
    memcpy(r + n, b, (m + 1) * sizeof(WCHAR));
  }
  return r;
}
static BOOL space(WCHAR c) {
  return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}
static WCHAR *trimspan(const WCHAR *p, size_t n) {
  while (n && space(*p)) {
    p++;
    n--;
  }
  while (n && space(p[n - 1]))
    n--;
  return span(p, n);
}
int core_cells(WCHAR c) {
  return c >= 0x1100 &&
                 (c <= 0x115f || (c >= 0x2e80 && c <= 0xa4cf) ||
                  (c >= 0xac00 && c <= 0xd7a3) ||
                  (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff01 && c <= 0xff60))
             ? 2
             : 1;
}
static WCHAR *slice(const WCHAR *p, int start, int end) {
  int col = 0;
  const WCHAR *a = NULL;
  const WCHAR *b = p;
  for (; *p; p++) {
    if (col >= start && col < end) {
      if (!a)
        a = p;
      b = p + 1;
    }
    col += core_cells(*p);
  }
  return trimspan(a ? a : L"", a ? (size_t)(b - a) : 0);
}
static BOOL dashed(const WCHAR *p) {
  size_t n = 0;
  while (space(*p))
    p++;
  while (*p == L'-') {
    p++;
    n++;
  }
  while (space(*p))
    p++;
  return !*p && n > 8;
}
void core_packages_free(CorePackages *p) {
  size_t i;
  if (!p)
    return;
  for (i = 0; i < p->count; i++) {
    core_free(p->items[i].name);
    core_free(p->items[i].id);
    core_free(p->items[i].version);
    core_free(p->items[i].available);
    core_free(p->items[i].source);
  }
  core_free(p->items);
  ZeroMemory(p, sizeof(*p));
}
BOOL core_parse(const CoreBytes *bytes, CorePackages *out) {
  WCHAR *input;
  WCHAR *at;
  WCHAR *previous = NULL;
  WCHAR *names[32] = {0};
  int starts[32], columns = 0;
  size_t cap = 0;
  BOOL ok = TRUE;
  ZeroMemory(out, sizeof(*out));
  if (!bytes || bytes->length > LIMIT)
    return FALSE;
  input = core_wide(bytes->data, bytes->length);
  if (!input)
    return FALSE;
  for (at = input; *at;) {
    WCHAR *end = wcschr(at, L'\n');
    WCHAR *p;
    WCHAR *clean;
    WCHAR *dst;
    size_t len = end ? (size_t)(end - at) : wcslen(at);
    while (len && at[len - 1] == L'\r')
      len--;
    WCHAR *lineEnd = at + len;
    for (p = at; p < lineEnd; p++)
      if (*p == L'\r')
        at = p + 1;
    len = end ? (size_t)(end - at) : wcslen(at);
    while (len && at[len - 1] == L'\r')
      len--;
    clean = span(at, len);
    if (!clean) {
      ok = FALSE;
      break;
    }
    dst = clean;
    for (p = clean; *p; p++) {
      if (*p == 27 && p[1] == L'[') {
        p += 2;
        while (*p && !(*p >= 0x40 && *p <= 0x7e))
          p++;
        if (!*p)
          break;
      } else if (*p != 0xfeff)
        *dst++ = *p;
    }
    *dst = 0;
    at = end ? end + 1 : at + wcslen(at);
    if (!*clean) {
      core_free(clean);
      continue;
    }
    if (dashed(clean)) {
      int col = 0;
      WCHAR *token = NULL;
      for (int i = 0; i < columns; i++)
        core_free(names[i]);
      columns = 0;
      if (previous)
        for (p = previous;; p++) {
          if (*p && !space(*p)) {
            if (!token)
              token = p;
          } else if (token) {
            if (columns < 32) {
              starts[columns] = col; /* recompute token cell offset */
              starts[columns] = 0;
              for (WCHAR *t = previous; t < token; t++)
                starts[columns] += core_cells(*t);
              names[columns] = span(token, (size_t)(p - token));
              if (!names[columns])
                ok = FALSE;
              columns++;
            }
            token = NULL;
          }
          if (!*p)
            break;
          col += core_cells(*p);
        }
    } else if (columns >= 3) {
      CorePackage item = {0};
      for (int i = 0; i < columns; i++) {
        WCHAR *v =
            slice(clean, starts[i], i + 1 < columns ? starts[i + 1] : INT_MAX);
        if (!v) {
          ok = FALSE;
          break;
        }
        if (i == 0)
          item.name = v;
        else if (i == 1)
          item.id = v;
        else if (i == 2)
          item.version = v;
        else if (!wcscmp(names[i], L"Available")) {
          core_free(item.available);
          item.available = v;
        } else if (!wcscmp(names[i], L"Source")) {
          core_free(item.source);
          item.source = v;
        } else
          core_free(v);
      }
      if (!item.available)
        item.available = core_copy(L"");
      if (!item.source)
        item.source = core_copy(L"");
      if (!item.available || !item.source)
        ok = FALSE;
      if (ok && item.name && *item.name && item.id && *item.id &&
          item.version && *item.version &&
          (!wcspbrk(item.id, L" \t") || !wcsncmp(item.id, L"ARP\\", 4)) &&
          !wcspbrk(item.version, L" \t") && wcscmp(item.id, L"Id")) {
        if (out->count == cap) {
          size_t next = cap ? cap * 2 : 32;
          CorePackage *items = next <= SIZE_MAX / sizeof(*items)
                                   ? alloc(next * sizeof(*items))
                                   : NULL;
          if (!items)
            ok = FALSE;
          else {
            if (out->count)
              memcpy(items, out->items, out->count * sizeof(*items));
            core_free(out->items);
            out->items = items;
            cap = next;
          }
        }
        if (ok) {
          out->items[out->count++] = item;
          ZeroMemory(&item, sizeof(item));
        }
      }
      core_free(item.name);
      core_free(item.id);
      core_free(item.version);
      core_free(item.available);
      core_free(item.source);
    }
    core_free(previous);
    previous = clean;
    if (!ok)
      break;
  }
  for (int i = 0; i < columns; i++)
    core_free(names[i]);
  core_free(previous);
  core_free(input);
  if (!ok)
    core_packages_free(out);
  return ok;
}
void core_command_free(CoreCommand *c) {
  for (UINT i = 0; i < c->count; i++)
    core_free(c->args[i]);
  core_free(c->args);
  ZeroMemory(c, sizeof(*c));
}
BOOL core_command_create(CoreCommand *c, const WCHAR *const *args, UINT count) {
  ZeroMemory(c, sizeof(*c));
  if (count > 1024 || (!args && count))
    return FALSE;
  c->args = alloc((size_t)count * sizeof(WCHAR *));
  if (count && !c->args)
    return FALSE;
  for (UINT i = 0; i < count; i++) {
    if (!args[i] || (c->args[i] = core_copy(args[i])) == NULL) {
      c->count = i;
      core_command_free(c);
      return FALSE;
    }
  }
  c->count = count;
  return TRUE;
}
BOOL core_package_command(CoreCommand *c, const CorePackage *p, int view) {
  const WCHAR *a[10];
  UINT n = 0;
  if (!p || !p->id || !*p->id || view < 0 || view > 2)
    return FALSE;
  a[n++] = view == CORE_REMOTE    ? L"install"
           : view == CORE_UPDATES ? L"upgrade"
                                  : L"uninstall";
  a[n++] = L"--id";
  a[n++] = p->id;
  a[n++] = L"--exact";
  a[n++] = L"--accept-source-agreements";
  a[n++] = L"--disable-interactivity";
  if (p->source && *p->source) {
    a[n++] = L"--source";
    a[n++] = p->source;
  } else if (view == CORE_REMOTE) {
    a[n++] = L"--source";
    a[n++] = L"winget";
  }
  if (view != CORE_INSTALLED)
    a[n++] = L"--accept-package-agreements";
  return core_command_create(c, a, n);
}
BOOL core_listing_command(CoreCommand *c, int view, const WCHAR *q) {
  const WCHAR *a[12];
  UINT n = 0;
  WCHAR *query = NULL;
  if (view < 0 || view > 2)
    return FALSE;
  if (view == CORE_REMOTE) {
    query=trimspan(q?q:L"",q?wcslen(q):0);
    if(!query)return FALSE;
  }
  a[n++] = view == CORE_REMOTE ? L"search" : L"list";
  if (view == CORE_UPDATES)
    a[n++] = L"--upgrade-available";
  if (view == CORE_REMOTE && *query) {
    a[n++] = L"--query";
    a[n++] = query;
  }
  if (view == CORE_REMOTE) {
    a[n++] = L"--source";
    a[n++] = L"winget";
    a[n++] = L"--count";
    a[n++] = L"1000";
  }
  a[n++] = L"--accept-source-agreements";
  a[n++] = L"--disable-interactivity";
  BOOL ok=core_command_create(c,a,n);core_free(query);return ok;
}
WCHAR *core_listing_key(int view, const WCHAR *q) {
  WCHAR *t;
  WCHAR *r;
  if (view < 0 || view > 2)
    return NULL;
  t = trimspan(q ? q : L"", q ? wcslen(q) : 0);
  if (!t)
    return NULL;
  for (WCHAR *p = t; *p; p++)
    *p = (WCHAR)towlower(*p);
  r = view == 0 ? join(L"search:", t)
                : core_copy(view == 1 ? L"installed" : L"updates");
  core_free(t);
  return r;
}
BOOL core_init_cache(BOOL portable) {
  WCHAR p[32768];
  DWORD n;
  WCHAR *dir;
  if (portable) {
    n = GetModuleFileNameW(NULL, p, 32768);
    if (!n || n >= 32768)
      return FALSE;
    WCHAR *slash = wcsrchr(p, L'\\');
    if (!slash)
      return FALSE;
    *slash = 0;
    dir = join(p, L"\\cache");
  } else {
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, 32768);
    if (!n || n >= 32768) {
      n = GetTempPathW(32768, p);
      if (!n || n >= 32768)
        return FALSE;
    }
    dir = join(p, L"\\WingetSwift\\cache");
  }
  if (!dir)
    return FALSE;
  core_free(cache_dir);
  cache_dir = dir;
  return TRUE;
}
static WCHAR *cache_path(const WCHAR *key) {
  uint64_t h = 14695981039346656037ull;
  int n;
  char *b;
  WCHAR hex[24] = L"\\";
  WCHAR digits[17];
  int at = 16;
  if (!cache_dir || !key)
    return NULL;
  n = WideCharToMultiByte(CP_UTF8, 0, key, -1, NULL, 0, NULL, NULL);
  if (!n)
    return NULL;
  b = alloc(n);
  if (!b)
    return NULL;
  if (!WideCharToMultiByte(CP_UTF8, 0, key, -1, b, n, NULL, NULL)) {
    core_free(b);
    return NULL;
  }
  for (int i = 0; i < n - 1; i++) {
    h ^= (unsigned char)b[i];
    h *= 1099511628211ull;
  }
  core_free(b);
  digits[16] = 0;
  do {
    digits[--at] = L"0123456789abcdef"[h & 15];
    h >>= 4;
  } while (h);
  wcscpy_s(hex, 24, L"\\");
  wcscat_s(hex, 24, digits + at);
  wcscat_s(hex, 24, L".cache");
  return join(cache_dir, hex);
}
static LONGLONG now_seconds(void) {
  FILETIME t;
  ULARGE_INTEGER n;
  GetSystemTimeAsFileTime(&t);
  n.LowPart = t.dwLowDateTime;
  n.HighPart = t.dwHighDateTime;
  return (LONGLONG)(n.QuadPart / 10000000ull) - 11644473600ll;
}
BOOL core_cache_read(const WCHAR *key, CoreCached *out) {
  WCHAR *path = cache_path(key);
  HANDLE f;
  LARGE_INTEGER size;
  DWORD got;
  char *bytes;
  size_t at = 0;
  LONGLONG stamp = 0;
  ZeroMemory(out, sizeof(*out));
  if (!path)
    return FALSE;
  f = CreateFileW(path, GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, 0, NULL);
  core_free(path);
  if (f == INVALID_HANDLE_VALUE)
    return TRUE;
  if (!GetFileSizeEx(f, &size) || size.QuadPart < 2 ||
      size.QuadPart > LIMIT + 21) {
    CloseHandle(f);
    return TRUE;
  }
  bytes = alloc((size_t)size.QuadPart + 1);
  if (!bytes) {
    CloseHandle(f);
    return FALSE;
  }
  BOOL read = ReadFile(f, bytes, (DWORD)size.QuadPart, &got, NULL);
  CloseHandle(f);
  if (!read || got != size.QuadPart) {
    core_free(bytes);
    return TRUE;
  }
  while (at < (size_t)got && bytes[at] >= '0' && bytes[at] <= '9') {
    int d = bytes[at++] - '0';
    if (stamp > (LLONG_MAX - d) / 10) {
      core_free(bytes);
      return TRUE;
    }
    stamp = stamp * 10 + d;
  }
  if (!at || at >= got || bytes[at] != '\n' || got - at - 1 > LIMIT) {
    core_free(bytes);
    return TRUE;
  }
  at++;
  memmove(bytes, bytes + at, got - at);
  bytes[got - at] = 0;
  out->exists = TRUE;
  out->age = now_seconds() - stamp;
  out->output.data = bytes;
  out->output.length = got - at;
  return TRUE;
}
static void prune(BOOL all) {
  WCHAR *pattern;
  if (!cache_dir)
    return;
  pattern = join(cache_dir, L"\\*.cache");
  if (!pattern)
    return;
  for (;;) {
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(pattern, &d);
    size_t count = 0;
    uint64_t oldest = UINT64_MAX;
    WCHAR chosen[MAX_PATH] = {0};
    if (h == INVALID_HANDLE_VALUE)
      break;
    do {
      if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        uint64_t t = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) |
                     d.ftLastWriteTime.dwLowDateTime;
        count++;
        if (all) {
          WCHAR *p = join(cache_dir, L"\\");
          WCHAR *full = p ? join(p, d.cFileName) : NULL;
          if (full)
            DeleteFileW(full);
          core_free(full);
          core_free(p);
        } else if (t < oldest) {
          oldest = t;
          wcscpy_s(chosen, MAX_PATH, d.cFileName);
        }
      }
    } while (FindNextFileW(h, &d));
    FindClose(h);
    if (all || count <= 128)
      break;
    WCHAR *p = join(cache_dir, L"\\");
    WCHAR *full = p ? join(p, chosen) : NULL;
    BOOL removed = full && DeleteFileW(full);
    core_free(full);
    core_free(p);
    if (!removed)
      break;
  }
  core_free(pattern);
}
void core_cache_invalidate(void) { prune(TRUE); }
BOOL core_cache_write(const WCHAR *key, const CoreBytes *out) {
  WCHAR *path;
  WCHAR *tmp;
  WCHAR suffix[80];
  char stamp[32];
  HANDLE f;
  DWORD wrote;
  BOOL ok;
  static LONG sequence;
  if (!cache_dir || !out || out->length > LIMIT || (!out->data && out->length))
    return FALSE;
  SHCreateDirectoryExW(NULL, cache_dir, NULL);
  DWORD attr = GetFileAttributesW(cache_dir);
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    return FALSE;
  path = cache_path(key);
  if (!path)
    return FALSE;
  swprintf_s(suffix, 80, L".%lu.%ld.tmp", GetCurrentProcessId(),
             InterlockedIncrement(&sequence));
  tmp = join(path, suffix);
  if (!tmp) {
    core_free(path);
    return FALSE;
  }
  f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  ok = f != INVALID_HANDLE_VALUE;
  if (ok) {
    _i64toa_s(now_seconds(), stamp, 32, 10);
    size_t n = strlen(stamp);
    stamp[n++] = '\n';
    ok = WriteFile(f, stamp, (DWORD)n, &wrote, NULL) && wrote == n;
    if (ok && out->length)
      ok = WriteFile(f, out->data, (DWORD)out->length, &wrote, NULL) &&
           wrote == out->length;
    if (ok)
      ok = FlushFileBuffers(f);
    CloseHandle(f);
  }
  if (ok)
    ok = MoveFileExW(tmp, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!ok)
    DeleteFileW(tmp);
  core_free(tmp);
  core_free(path);
  if (ok)
    prune(FALSE);
  return ok;
}
static BOOL microsoft_signed(const WCHAR *path) {
  WINTRUST_FILE_INFO f = {0};
  WINTRUST_DATA d = {0};
  GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  BOOL identity = FALSE;
  f.cbStruct = sizeof(f);
  f.pcwszFilePath = path;
  d.cbStruct = sizeof(d);
  d.dwUIChoice = WTD_UI_NONE;
  d.fdwRevocationChecks = WTD_REVOKE_NONE;
  d.dwUnionChoice = WTD_CHOICE_FILE;
  d.pFile = &f;
  d.dwStateAction = WTD_STATEACTION_VERIFY;
  d.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
  if (WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policy, &d) ==
      ERROR_SUCCESS) {
    CRYPT_PROVIDER_DATA *provider =
        WTHelperProvDataFromStateData(d.hWVTStateData);
    CRYPT_PROVIDER_SGNR *signer =
        provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : NULL;
    if (signer && signer->csCertChain) {
      WCHAR name[128];
      if (CertGetNameStringW(signer->pasCertChain[0].pCert,
                             CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, 128))
        identity = !wcscmp(name, L"Microsoft Corporation");
    }
  }
  d.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policy, &d);
  return identity;
}
static WCHAR *trusted_path(void) {
  UINT32 count = 0, n = 0;
  PWSTR *names;
  WCHAR *buffer;
  WCHAR *result = NULL;
  if (verified_exe && GetFileAttributesW(verified_exe) != INVALID_FILE_ATTRIBUTES)
    return core_copy(verified_exe);
  if (GetPackagesByPackageFamily(L"Microsoft.DesktopAppInstaller_8wekyb3d8bbwe",
                                 &count, NULL, &n,
                                 NULL) != ERROR_INSUFFICIENT_BUFFER ||
      !count || !n)
    return NULL;
  if (count > SIZE_MAX / sizeof(PWSTR) || n > SIZE_MAX / sizeof(WCHAR))
    return NULL;
  names = alloc((size_t)count * sizeof(PWSTR));
  buffer = alloc((size_t)n * sizeof(WCHAR));
  if (!names || !buffer)
    goto done;
  if (GetPackagesByPackageFamily(L"Microsoft.DesktopAppInstaller_8wekyb3d8bbwe",
                                 &count, names, &n, buffer) != ERROR_SUCCESS)
    goto done;
  for (UINT32 i = 0; i < count; i++) {
    UINT32 size = 0;
    WCHAR *p;
    WCHAR *exe;
    if (GetPackagePathByFullName(names[i], &size, NULL) !=
            ERROR_INSUFFICIENT_BUFFER ||
        !size || size > SIZE_MAX / sizeof(WCHAR))
      continue;
    p = alloc((size_t)size * sizeof(WCHAR));
    if (!p)
      break;
    if (GetPackagePathByFullName(names[i], &size, p) != ERROR_SUCCESS) {
      core_free(p);
      continue;
    }
    exe = join(p, L"\\winget.exe");
    core_free(p);
    if (exe && microsoft_signed(exe)) {
      result = exe;
      break;
    }
    core_free(exe);
  }
done:
  core_free(names);
  core_free(buffer);
  if (result) {
    core_free(verified_exe);
    verified_exe = core_copy(result);
  }
  return result;
}
/* Every argument is quoted using the Windows CommandLineToArgvW rules. */
static WCHAR *quoted(const WCHAR *s) {
  size_t n = wcslen(s);
  WCHAR *r;
  WCHAR *p;
  size_t slash = 0;
  if (n > (SIZE_MAX / sizeof(WCHAR) - 3) / 2)
    return NULL;
  r = alloc((2 * n + 3) * sizeof(WCHAR));
  if (!r)
    return NULL;
  p = r;
  *p++ = L'"';
  for (; *s; s++) {
    if (*s == L'\\') {
      slash++;
      continue;
    }
    size_t k = slash * (*s == L'"' ? 2 : 1);
    while (k--)
      *p++ = L'\\';
    if (*s == L'"')
      *p++ = L'\\';
    *p++ = *s;
    slash = 0;
  }
  slash *= 2;
  while (slash--)
    *p++ = L'\\';
  *p++ = L'"';
  *p = 0;
  return r;
}
void core_result_free(CoreResult *r) {
  core_free(r->output.data);
  ZeroMemory(r, sizeof(*r));
}
BOOL core_list_success(const CoreResult *r) {
  return r && !r->timedOut && !r->canceled && !r->error &&
         (r->code == 0 || r->code == 0x8a150014);
}
CoreResult core_run(const CoreCommand *c, DWORD seconds,
                    volatile LONG *cancel) {
  CoreResult r = {0};
  WCHAR *exe = NULL;
  WCHAR *cmd = NULL;
  HANDLE rd = NULL, wr = NULL, input = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION pi = {0};
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  STARTUPINFOW si = {0};
  ULONGLONG start, stopped = 0;
  size_t capacity = 0;
  BOOL exited = FALSE, pipeClosed = FALSE;
  r.code = ERROR_GEN_FAILURE;
  if (!c || c->count > 1024) {
    r.error = L"Invalid command";
    return r;
  }
  if (cancel && InterlockedCompareExchange(cancel, 0, 0)) {
    r.code = ERROR_CANCELLED;
    r.canceled = TRUE;
    return r;
  }
  exe = trusted_path();
  if (!exe) {
    r.error =
        L"Microsoft-signed App Installer is unavailable. Repair App Installer.";
    return r;
  }
  cmd = quoted(exe);
  if (!cmd)
    goto memory;
  for (UINT i = 0; i < c->count; i++) {
    WCHAR *q;
    WCHAR *spaced;
    WCHAR *next;
    if (!c->args[i]) {
      r.error = L"Invalid argument";
      goto done;
    }
    q = quoted(c->args[i]);
    spaced = q ? join(L" ", q) : NULL;
    next = spaced ? join(cmd, spaced) : NULL;
    core_free(q);
    core_free(spaced);
    if (!next)
      goto memory;
    core_free(cmd);
    cmd = next;
  }
  if (wcslen(cmd) >= 32767) {
    r.error = L"Command is too long";
    r.code = ERROR_BAD_LENGTH;
    goto done;
  }
  if (!CreatePipe(&rd, &wr, &sa, 0) ||
      !SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) {
    r.error = L"Cannot create output pipe";
    r.code = GetLastError();
    goto done;
  }
  input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &sa, OPEN_EXISTING, 0, NULL);
  if (input == INVALID_HANDLE_VALUE) {
    r.error = L"Cannot open process input";
    r.code = GetLastError();
    goto done;
  }
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = input;
  si.hStdOutput = wr;
  si.hStdError = wr;
  if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL,
                      &si, &pi)) {
    r.error = L"Cannot start WinGet";
    r.code = GetLastError();
    goto done;
  }
  CloseHandle(wr);
  wr = NULL;
  CloseHandle(input);
  input = INVALID_HANDLE_VALUE;
  start = GetTickCount64();
  for (;;) {
    DWORD available = 0, got = 0;
    char chunk[8192];
    if (!r.canceled && !r.timedOut && !exited) {
      if (cancel && InterlockedCompareExchange(cancel, 0, 0)) {
        r.canceled = TRUE;
        stopped = GetTickCount64();
        TerminateProcess(pi.hProcess, ERROR_CANCELLED);
      } else if (GetTickCount64() - start >= (ULONGLONG)seconds * 1000) {
        r.timedOut = TRUE;
        stopped = GetTickCount64();
        TerminateProcess(pi.hProcess, ERROR_TIMEOUT);
      }
    }
    if ((r.canceled || r.timedOut || exited) &&
        GetTickCount64() - stopped >= 5000)
      break;
    if (!pipeClosed && !PeekNamedPipe(rd, NULL, 0, NULL, &available, NULL)) {
      DWORD error=GetLastError();
      if(error==ERROR_BROKEN_PIPE)pipeClosed=TRUE;
      else {r.error=L"Output pipe failed; an installer may still run.";r.code=error;TerminateProcess(pi.hProcess,error);break;}
    }
    if (!pipeClosed && available) {
      BOOL read=ReadFile(rd, chunk,
                    available > sizeof(chunk) ? sizeof(chunk) : available, &got,
                    NULL);
      if (!read || !got) {
        DWORD error=read?ERROR_BROKEN_PIPE:GetLastError();
        if(error==ERROR_BROKEN_PIPE){pipeClosed=TRUE;continue;}
        r.error=L"Output read failed; an installer may still run.";r.code=error;TerminateProcess(pi.hProcess,error);break;
      }
      size_t keep = got;
      if (keep > LIMIT - r.output.length)
        keep = LIMIT - r.output.length;
      if (keep) {
        size_t needed = r.output.length + keep + 1;
        if (needed > capacity) {
          size_t next = capacity ? capacity * 2 : 8192;
          if (next < needed)
            next = needed;
          if (next > LIMIT + 1)
            next = LIMIT + 1;
          char *b = alloc(next);
          if (!b) {
            r.error = L"Output allocation failed";
            TerminateProcess(pi.hProcess, ERROR_OUTOFMEMORY);
            r.code = ERROR_OUTOFMEMORY;
            break;
          }
          if (r.output.length)
            memcpy(b, r.output.data, r.output.length);
          core_free(r.output.data);
          r.output.data = b;
          capacity = next;
        }
        if (!r.output.data) {
          r.error = L"Output allocation failed";
          r.code = ERROR_OUTOFMEMORY;
          TerminateProcess(pi.hProcess, ERROR_OUTOFMEMORY);
          break;
        }
        memcpy(r.output.data + r.output.length, chunk, keep);
        r.output.length += keep;
        r.output.data[r.output.length] = 0;
      }
      continue;
    }
    if (exited)
      break;
    DWORD wait = WaitForSingleObject(pi.hProcess, 20);
    if (wait == WAIT_OBJECT_0) {
      exited = TRUE;
      stopped = GetTickCount64();
    } else if (wait == WAIT_FAILED) {
      r.error = L"Process wait failed; an installer may still run.";
      r.code=GetLastError();TerminateProcess(pi.hProcess,r.code);
      break;
    }
  }
  WaitForSingleObject(pi.hProcess, 5000);
  if (!r.error && !GetExitCodeProcess(pi.hProcess, &r.code)) {
    r.error = L"Cannot read process status";
    r.code = GetLastError();
  }
  if (r.canceled)
    r.code = ERROR_CANCELLED;
  else if (r.timedOut)
    r.code = ERROR_TIMEOUT;
  goto done;
memory:
  r.error = L"Allocation failed";
  r.code = ERROR_OUTOFMEMORY;
done:
  if (pi.hThread)
    CloseHandle(pi.hThread);
  if (pi.hProcess)
    CloseHandle(pi.hProcess);
  if (rd)
    CloseHandle(rd);
  if (wr)
    CloseHandle(wr);
  if (input != INVALID_HANDLE_VALUE)
    CloseHandle(input);
  core_free(exe);
  core_free(cmd);
  return r;
}
