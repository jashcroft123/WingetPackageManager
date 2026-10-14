#pragma once
int tui_tests() {
  int failures = 0;
  std::ofstream report("tui-test-results.txt");
  auto check = [&](bool ok, const char *name) {
    report << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok)
      failures++;
  };
  check(quote(L"a b") == L"\"a b\"" && quote(L"x\\") == L"\"x\\\\\"" &&
            quote(L"a\"b") == L"\"a\\\"b\"",
        "Windows argument quoting");
  auto rows =
      parse("Name    Id       Version\n--------------------------\nAlpha   "
            "A.Alpha  1.0\nName    Id       "
            "Version\n--------------------------\nBeta    B.Beta   2.0\n");
  check(rows.size() == 2 && rows[1].id == L"B.Beta", "multiple tables");
  check(parse("\x1b[32mName    Id       "
              "Version\x1b[0m\n--------------------------\nnoise\rAlpha   "
              "A.Alpha  1.0\r\n")
                .size() == 1,
        "ANSI and carriage return parsing");
  State s;
  s.view = INSTALLED;
  s.packages = {{L"Alpha", L"A.Alpha", L"1", L"", L"winget"},
                {L"Beta", L"B.Beta", L"1", L"", L"winget"},
                {L"Gamma", L"C.Gamma", L"1", L"", L"winget"}};
  s.checked.assign(3, false);
  s.filter();
  s.select(0, false);
  s.select(2, true);
  check(std::count(s.checked.begin(), s.checked.end(), true) == 3,
        "inclusive range selection");
  s.action();
  check(s.confirm && s.confirm->batch.size() == 3 &&
            s.confirm->batch[0][0] == L"uninstall",
        "confirmed command snapshot");
  s.query = L"Beta";
  s.filter();
  check(s.visible.size() == 1 && s.visible[0] == 1 &&
            std::count(s.checked.begin(),s.checked.end(),true)==0,
        "installed filtering clears hidden selections");
  s.packages[0].id = L"Changed";
  check(s.confirm && s.confirm->batch[0][2] == L"A.Alpha",
        "snapshot independent of live rows");
  KEY_EVENT_RECORD key{};
  key.bKeyDown = TRUE;
  key.wVirtualKeyCode = 'N';
  key.uChar.UnicodeChar = L'n';
  keypress(s, key);
  check(!s.confirm, "confirmation cancellation");
  s.packages[0].id=L"A...";s.checked.assign(3,false);s.checked[0]=true;s.action();
  check(!s.confirm&&s.status.find(L"truncated")!=std::wstring::npos,"truncated IDs cannot reach confirmation");
  s.packages[0].id=L"A.Alpha";s.checked.assign(3,false);
  s.editing = 2;
  s.edit = L"installed-packages.json";
  key.wVirtualKeyCode = 'A';
  key.dwControlKeyState = LEFT_CTRL_PRESSED;
  key.uChar.UnicodeChar = 1;
  keypress(s, key);
  check(s.edit.empty(), "Ctrl+A clears path editor");
  key.wVirtualKeyCode = 'C';
  key.uChar.UnicodeChar = 3;
  check(!keypress(s, key),
        "Ctrl+C quits read-only state, including path editor");
  s.worker = (HANDLE)1; // State-only fixture; never passed to a Windows wait.
  s.job = std::make_unique<Job>();
  s.job->mutation = true;
  check(keypress(s, key), "Ctrl+C is blocked during a mutation");
  s.worker = nullptr;
  s.job.reset();
  auto exported = transferJob(false, L"snapshot.json");
  auto imported = transferJob(true, L"snapshot.json");
  check(!exported->mutation && !exported->listing &&
            exported->args.back() == L"--include-versions",
        "export is nonlisting read-only and includes versions");
  check(imported->mutation && !imported->listing &&
            std::find(imported->args.begin(), imported->args.end(),
                      L"--ignore-versions") != imported->args.end(),
        "import ignores saved versions and is a confirmed mutation");
  s.worker = (HANDLE)1;
  s.job = std::make_unique<Job>();
  s.job->mutation = true;
  auto queued = std::make_unique<Job>();
  queued->listing = true;
  queued->key = L"updates";
  cancelRead = false;
  s.start(std::move(queued));
  check(s.pending && s.pending->key == L"updates" && !cancelRead.load(),
        "browsing queues a listing without canceling a mutation");
  s.job->mutation = false;
  s.job->listing = true;
  s.job->key = L"installed";
  s.load(false);
  check(!s.pending && !cancelRead.load() && s.job->generation == s.generation,
        "same query reuses the active check without canceling it");
  s.worker = nullptr;
  s.job.reset();
  int calls = 0;
  auto capture=runBatch({packageCommand(s.packages[0],REMOTE),packageCommand(s.packages[1],REMOTE)},[&](const Command&){++calls;return Result{ERROR_READ_FAULT,"capture failed",false,true};});
  check(calls==1&&capture.captureFailed&&capture.code==ERROR_READ_FAULT,"capture failure stops remaining batch");calls=0;
  auto batch = runBatch({packageCommand(s.packages[0], REMOTE),
                         packageCommand(s.packages[1], REMOTE)},
                        [&](const Command &) {
                          calls++;
                          return Result{ERROR_TIMEOUT, "timeout", true};
                        });
  check(calls == 1 && batch.output.find("1 not attempted") != std::string::npos,
        "batch stops after timeout");
  report << "No WinGet processes, cache mutations or UI automation are "
            "performed by these diagnostics.\nFailures: "
         << failures << '\n';
  return failures ? 1 : 0;
}
