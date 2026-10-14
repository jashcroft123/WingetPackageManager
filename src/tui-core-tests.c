#include "tui_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef WINGETSWIFT_DIAGNOSTICS
static FILE *report;
static int failed;
static void check(BOOL pass, const char *label) {
  fprintf(report, "%s %s\n", pass ? "PASS" : "FAIL", label);
  if (!pass)
    failed++;
}
static void parser(const char *raw, size_t count, const WCHAR *id,
                   const char *label) {
  CoreBytes b = {(char *)raw, strlen(raw)};
  CorePackages p = {0};
  BOOL ok = core_parse(&b, &p);
  check(ok && p.count == count &&
            (!id || (p.count && !wcscmp(p.items[0].id, id))),
        label);
  core_packages_free(&p);
}
int core_tests(void) {
  CoreCommand c = {0};
  CorePackage package = {L"Example", L"Vendor.Exact\"Id\\", L"1", L"2",
                         L"custom"};
  CoreCached cached = {0};
  CoreBytes bytes = {"sample\0binary", 13};
  WCHAR *key;
  WCHAR original[32768];
  WCHAR temp[MAX_PATH];
  WCHAR local[MAX_PATH];
  WCHAR directory[MAX_PATH];
  volatile LONG cancel = 1;
  failed = 0;
  if (fopen_s(&report, "core-test-results.txt", "w") || !report)
    return 1;
  parser("Name             Id            Version  "
         "Source\n---------------------------------------------------\nExample "
         "package  Vendor.Exact  1.2      winget\n",
         1, L"Vendor.Exact", "fixed column parser");
  parser("\x1b[32mName    Id       "
         "Version\x1b[0m\n--------------------------\nnoise\rAlpha   A.Alpha  "
         "1.0\r\n",
         1, L"A.Alpha", "ANSI and progress carriage return");
  parser("Name    Id       Version\n--------------------------\nAlpha   "
         "A.Alpha  1.0\nName    Id       "
         "Version\n--------------------------\nBeta    B.Beta   2.0\n",
         2, L"A.Alpha", "multiple tables without header rows");
  parser("Name    Id       "
         "Version\n--------------------------\n\xe4\xb8\xad\xe6\x96\x87    "
         "A.CJK    1.0\n",
         1, L"A.CJK", "UTF-8 double width columns");
  parser("No installed package found matching input criteria.", 0, NULL,
         "empty listing");
  check(core_package_command(&c, &package, CORE_REMOTE) && c.count == 9 &&
            !wcscmp(c.args[2], package.id) && !wcscmp(c.args[3], L"--exact") &&
            !wcscmp(c.args[7], L"custom"),
        "exact ID and explicit source preserved, including quote and slash");
  core_command_free(&c);
  check(core_listing_command(&c, CORE_REMOTE, L"editor") && c.count == 9 &&
            !wcscmp(c.args[5], L"--count") && !wcscmp(c.args[6], L"1000"),
        "remote listing bounded to 1000 rows");
  core_command_free(&c);
  check(core_listing_command(&c, CORE_INSTALLED, L"local filter") && c.count == 3 && !wcscmp(c.args[0], L"list"), "local filter never narrows the shared installed cache");
  core_command_free(&c);
  check(core_listing_command(&c, CORE_UPDATES, L"local filter") && c.count == 4 && !wcscmp(c.args[0], L"list") && !wcscmp(c.args[1], L"--upgrade-available"), "update listing is read-only and uses default pin filtering");
  core_command_free(&c);
  key = core_listing_key(CORE_REMOTE, L" \tEdItOr\r\n");
  check(key && !wcscmp(key, L"search:editor"), "normalized legacy search key");
  core_free(key);
  key = core_listing_key(CORE_UPDATES, L"ignored");
  check(key && !wcscmp(key, L"updates"), "legacy update key");
  core_free(key);
  CoreResult r = core_run(&c, 1, &cancel);
  check(r.canceled && r.code == ERROR_CANCELLED,
        "pre-cancel performs no trusted lookup or process launch");
  core_result_free(&r);
  GetEnvironmentVariableW(L"LOCALAPPDATA", original, 32768);
  GetTempPathW(MAX_PATH, temp);
  swprintf_s(local, MAX_PATH, L"%sWingetSwift-CoreTests-%lu", temp,
             GetCurrentProcessId());
  SetEnvironmentVariableW(L"LOCALAPPDATA", local);
  check(core_init_cache(FALSE), "isolated cache init");
  SetEnvironmentVariableW(L"LOCALAPPDATA", *original ? original : NULL);
  check(core_cache_write(L"roundtrip", &bytes) &&
            core_cache_read(L"roundtrip", &cached) && cached.exists &&
            cached.output.length == bytes.length &&
            !memcmp(cached.output.data, bytes.data, bytes.length),
        "atomic cache binary round trip");
  core_free(cached.output.data);
  swprintf_s(directory,MAX_PATH,L"%s\\WingetSwift\\cache\\d9f4e56fe22c484.cache",local);
  const char* badHeaders[]={"-1\ninvalid","999999999999999999999999999999\ninvalid","garbage\ninvalid"};
  for(size_t i=0;i<_countof(badHeaders);++i){
    HANDLE file=CreateFileW(directory,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD wrote=0;
    BOOL written=file!=INVALID_HANDLE_VALUE&&WriteFile(file,badHeaders[i],(DWORD)strlen(badHeaders[i]),&wrote,NULL);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    check(written&&core_cache_read(L"roundtrip",&cached)&&!cached.exists,"malformed or overflowing cache timestamp ignored");
    core_free(cached.output.data);
  }
  for (int i = 0; i < 130; i++) {
    WCHAR k[40];
    swprintf_s(k, 40, L"prune:%d", i);
    if (!core_cache_write(k, &bytes)) {
      check(FALSE, "pruning cache write");
      break;
    }
  }
  swprintf_s(directory, MAX_PATH, L"%s\\WingetSwift\\cache\\*.cache", local);
  WIN32_FIND_DATAW data;
  HANDLE find = FindFirstFileW(directory, &data);
  int count = 0;
  if (find != INVALID_HANDLE_VALUE) {
    do {
      if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        count++;
    } while (FindNextFileW(find, &data));
    FindClose(find);
  }
  check(count == 128, "cache pruning retains 128 entries");
  core_cache_invalidate();
  check(core_cache_read(L"roundtrip", &cached) && !cached.exists,
        "cache invalidation");
  core_free(cached.output.data);
  swprintf_s(directory, MAX_PATH, L"%s\\WingetSwift\\cache", local);
  RemoveDirectoryW(directory);
  swprintf_s(directory, MAX_PATH, L"%s\\WingetSwift", local);
  RemoveDirectoryW(directory);
  RemoveDirectoryW(local);
  core_init_cache(FALSE);
  fprintf(
      report,
      "Process execution and shadow WinGet runtime test skipped: Application "
      "Control restriction; core never searches PATH.\nFailures: %d\n",
      failed);
  fclose(report);
  report = NULL;
  return failed ? 1 : 0;
}
#endif
