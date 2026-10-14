#include "backend.hpp"
#include <memory>
#include <process.h>

struct Console {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE),
         original = GetStdHandle(STD_OUTPUT_HANDLE),
         screen = INVALID_HANDLE_VALUE;
  DWORD inputMode = 0;
  CONSOLE_CURSOR_INFO cursor{};
  wchar_t title[32768]{};
  Console() {
    if (!GetConsoleMode(input, &inputMode))
      throw std::runtime_error("Run Winget Swift in a Windows console.");
    GetConsoleTitleW(title, 32768);
    GetConsoleCursorInfo(original, &cursor);
    screen = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
    if (screen == INVALID_HANDLE_VALUE)
      throw std::runtime_error("Cannot create console screen");
    if (!SetConsoleActiveScreenBuffer(screen)) {
      CloseHandle(screen);
      screen = INVALID_HANDLE_VALUE;
      throw std::runtime_error("Cannot activate screen");
    }
    SetConsoleMode(input, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
    CONSOLE_CURSOR_INFO hidden{1, FALSE};
    SetConsoleCursorInfo(screen, &hidden);
    SetConsoleTitleW(L"Winget Swift - TUI");
  }
  ~Console() {
    SetConsoleMode(input, inputMode);
    SetConsoleActiveScreenBuffer(original);
    SetConsoleCursorInfo(original, &cursor);
    SetConsoleTitleW(title);
    if (screen != INVALID_HANDLE_VALUE)
      CloseHandle(screen);
  }
};
struct Job {
  Command args;
  std::vector<Command> batch;
  std::wstring key;
  unsigned generation = 0;
  bool mutation = false, listing = false, canceled = false;
  Result result;
  std::string error;
  std::atomic_size_t progress{0};
  steady_clock::time_point began = steady_clock::now();
};
static std::atomic_bool cancelRead{false};
static std::unique_ptr<Job> transferJob(bool importing,
                                        const std::wstring &path) {
  auto next = std::make_unique<Job>();
  next->mutation = importing;
  next->args = {importing ? L"import" : L"export",
                importing ? L"--import-file" : L"--output", path,
                L"--accept-source-agreements", L"--disable-interactivity"};
  if (importing) {
    next->args.push_back(L"--ignore-versions");
    next->args.push_back(L"--accept-package-agreements");
  } else
    next->args.push_back(L"--include-versions");
  return next;
}
static unsigned __stdcall execute(void *value) {
  auto &j = *(Job *)value;
  try {
    if (j.batch.empty())
      j.result = run(j.args, j.mutation ? 3600 : 120,
                     j.mutation ? nullptr : &cancelRead);
    else
      j.result = runBatch(j.batch, [&](const Command &a) {
        ++j.progress;
        return run(a, 3600);
      });
    j.canceled = !j.mutation && cancelRead.load();
    if (j.mutation)
      invalidate();
    else if (j.listing && !j.canceled && listSuccess(j.result)) {
      try {
        writeCache(j.key, j.result.output);
      } catch (const std::exception &e) {
        j.error = std::string("Cache unavailable: ") + e.what();
      }
    }
  } catch (const std::exception &e) {
    j.error = e.what();
    j.result.code = ERROR_GEN_FAILURE;
  }
  return 0;
}
struct State {
  int view = REMOTE;
  std::wstring query,
      status =
          L"Search remote packages with /, or press 2 for installed packages.",
      edit, editLabel;
  std::vector<Package> packages;
  std::vector<size_t> visible;
  std::vector<bool> checked;
  size_t focus = 0, anchor = 0, top = 0, logTop = 0;
  unsigned generation = 0;
  std::wstring key, mutationWarning;
  std::unique_ptr<Job> job, pending, confirm;
  HANDLE worker = nullptr;
  bool output = false;
  int editing = 0;
  std::string log, operationLog;
  ~State() {
    if (worker) {
      cancelRead = true;
      WaitForSingleObject(worker, INFINITE);
      CloseHandle(worker);
    }
  }
  void filter() {
    checked.assign(packages.size(), false);
    visible.clear();
    auto needle = lower(trim(query));
    for (size_t i = 0; i < packages.size(); i++)
      if (view == REMOTE || needle.empty() ||
          lower(packages[i].name + L" " + packages[i].id).find(needle) !=
              std::wstring::npos)
        visible.push_back(i);
    focus = visible.empty() ? 0 : std::min(focus, visible.size() - 1);
    anchor = visible.empty() ? 0 : std::min(anchor, visible.size() - 1);
    top = 0;
  }
  void select(size_t at, bool range) {
    if (visible.empty())
      return;
    if (range) {
      size_t a = std::min(anchor, at), b = std::max(anchor, at);
      for (size_t i = a; i <= b; i++)
        checked[visible[i]] = true;
    } else {
      checked[visible[at]] = !checked[visible[at]];
      anchor = at;
    }
  }
  void start(std::unique_ptr<Job> next) {
    if (worker) {
      if (job->mutation) {
        if (next->listing) {
          pending = std::move(next);
          status = L"Latest listing queued after the package operation.";
        } else
          status = L"Wait for the current package operation.";
        return;
      }
      pending = std::move(next);
      cancelRead = true;
      return;
    }
    cancelRead = false;
    job = std::move(next);
    job->began = steady_clock::now();
    uintptr_t h = _beginthreadex(nullptr, 0, execute, job.get(), 0, nullptr);
    if (!h) {
      status = L"Cannot start worker thread";
      job.reset();
      return;
    }
    worker = (HANDLE)h;
    status = job->mutation  ? L"Running package operation"
             : job->listing ? L"Loading packages"
                            : L"Exporting packages";
  }
  void load(bool force) {
    ++generation;
    key = view == REMOTE    ? L"search:" + lower(trim(query))
          : view == UPDATES ? L"updates"
                            : L"installed";
    if (!force && worker && job->listing && !cancelRead.load() &&
        job->key == key) {
      job->generation = generation;
      pending.reset();
      status = L"Loading packages";
      return;
    }
    if (view == REMOTE && trim(query).empty()) {
      packages.clear();
      checked.clear();
      filter();
      if (worker) {
        if (!job->mutation)
          cancelRead = true;
        pending.reset();
      }
      status = L"Search remote packages with /.";
      return;
    }
    auto cached = readCache(key);
    if (cached.exists) {
      packages = parse(cached.output);
      checked.assign(packages.size(), false);
      filter();
      status = L"Cached: " + wnumber(visible.size()) + L" packages";
    } else {
      packages.clear();
      checked.clear();
      filter();
    }
    int ttl = view == REMOTE ? 86400 : view == UPDATES ? 300 : 600;
    if (force || !cached.exists || cached.age < 0 || cached.age > ttl) {
      auto next = std::make_unique<Job>();
      next->listing = true;
      next->key = key;
      next->generation = generation;
      if (view == REMOTE)
        next->args = {L"search",
                      L"--query",
                      trim(query),
                      L"--source",
                      L"winget",
                      L"--count",
                      L"1000",
                      L"--accept-source-agreements",
                      L"--disable-interactivity"};
      else if (view == UPDATES)
        next->args = {L"list", L"--upgrade-available",
                      L"--accept-source-agreements",
                      L"--disable-interactivity"};
      else
        next->args = {L"list", L"--accept-source-agreements",
                      L"--disable-interactivity"};
      start(std::move(next));
    } else if (worker) {
      if (!job->mutation)
        cancelRead = true;
      pending.reset();
    }
  }
  void complete() {
    CloseHandle(worker);
    worker = nullptr;
    auto done = std::move(job);
    if (!done->canceled) {
      log = done->result.output;
      if (!done->error.empty())
        log += "\r\n" + done->error;
      if (!done->listing)
        operationLog = log;
      else if (!operationLog.empty())
        log = operationLog + "\r\n--- Package refresh ---\r\n" + log;
      logTop = 0;
      if (done->mutation) {
        mutationWarning =
            done->result.timeout
                ? L"Last operation timed out; installer may still run"
            : done->result.code || !done->error.empty()
                ? L"Last package operation failed; press l for output"
                : L"";
        status =
            done->result.timeout
                ? L"Timed out; installer may still run. Press l for output."
            : done->result.code ? L"Operation failed. Press l for output."
                                : L"Operation completed. Press l for output.";
        packages.clear();
        checked.clear();
        filter();
        pending.reset();
        load(true);
      } else if (!done->listing) {
        status = done->result.timeout ? L"Export timed out. Press l for output."
                 : done->result.code || !done->error.empty()
                     ? L"Export failed. Press l for output."
                     : L"Export completed. Press l for output.";
      } else if (done->generation == generation && done->key == key) {
        if (listSuccess(done->result)) {
          packages = parse(done->result.output);
          checked.assign(packages.size(), false);
          filter();
          status = L"Live: " + wnumber(visible.size()) + L" packages" +
                   (done->error.empty() ? L"" : L"; cache unavailable");
        } else
          status = L"Listing failed. Press l for output.";
      }
    }
    if (pending && !worker) {
      auto next = std::move(pending);
      start(std::move(next));
    }
  }
  void action() {
    if (worker) {
      status = L"Wait for background work before confirming an operation.";
      return;
    }
    auto next = std::make_unique<Job>();
    next->mutation = true;
    for (size_t i = 0; i < packages.size(); i++)
      if (checked[i]) {
        if(packages[i].id.find(L'\x2026')!=std::wstring::npos || packages[i].id.find(L"...")!=std::wstring::npos){
          status=L"A selected ID is truncated; narrow the search before applying.";return;
        }
        next->batch.push_back(packageCommand(packages[i], view));
      }
    if (next->batch.empty()) {
      status = L"Select packages with Space first.";
      return;
    }
    confirm = std::move(next);
    status = L"Confirm " + wnumber(confirm->batch.size()) +
             (view == REMOTE    ? L" installation(s)"
              : view == UPDATES ? L" update(s)"
                                : L" removal(s)") +
             L": y to proceed, n to cancel";
  }
};
static void draw(Console &c, State &s) {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(c.screen, &info))
    return;
  short width = info.srWindow.Right - info.srWindow.Left + 1,
        height = info.srWindow.Bottom - info.srWindow.Top + 1;
  if (width < 1 || height < 1)
    return;
  std::vector<CHAR_INFO> frame((size_t)width * height);
  for (auto &cell : frame) {
    cell.Char.UnicodeChar = L' ';
    cell.Attributes = 7;
  }
  auto line = [&](int row, const std::wstring &text, WORD color = 7) {
    if (row < 0 || row >= height)
      return;
    int col = 0;
    for (wchar_t ch : text) {
      if (col >= width)
        break;
      auto &cell = frame[(size_t)row * width + col];
      cell.Char.UnicodeChar = ch;
      cell.Attributes = color;
      int n = cells(ch);
      if (n == 2 && col + 1 < width)
        frame[(size_t)row * width + col + 1].Attributes =
            color | COMMON_LVB_TRAILING_BYTE;
      col += n;
    }
  };
  line(0, L"WinGet Swift | 1 Remote  2 Installed  3 Updates | / Search/filter",
       11);
  line(1,
       L"Space select  Shift range  Ctrl+A all  Enter action  r refresh  e "
       L"export  i import  l output  q quit",
       8);
  line(2, L"Query: " + s.query, 15);
  int rows = std::max(1, (int)height - 6);
  if (s.output) {
    const auto text = wide(s.log);
    size_t n = 0;
    int row = 3;
    for (size_t at = 0; at < text.size();) {
      size_t end = text.find(L'\n', at);
      if (end == std::wstring::npos)
        end = text.size();
      if (n++ >= s.logTop) {
        line(row++, cleanLine(text.substr(at, end - at)));
        if (row >= height - 3)
          break;
      }
      at = end + 1;
    }
  } else {
    if (s.focus < s.top)
      s.top = s.focus;
    if (s.focus >= s.top + (size_t)rows)
      s.top = s.focus - rows + 1;
    for (int r = 0; r < rows && s.top + r < s.visible.size(); r++) {
      size_t at = s.top + r, index = s.visible[at];
      auto &p = s.packages[index];
      std::wstring text = (s.checked[index] ? L"[x] " : L"[ ] ") + p.name +
                          L" | " + p.id + L" | " + p.version +
                          (p.available.empty() ? L"" : L" -> " + p.available) +
                          L" | " + p.source;
      line(3 + r, text, at == s.focus ? 112 : 7);
    }
  }
  std::wstring activity = s.status;
  if (!s.mutationWarning.empty())
    activity = s.mutationWarning + L" | " + activity;
  if (s.worker) {
    auto elapsed =
        duration_cast<seconds>(steady_clock::now() - s.job->began).count();
    activity =
        (s.job->mutation ? L"Operation " : L"Loading ") +
        wnumber(s.job->progress.load()) +
        (s.job->batch.empty() ? L"" : L"/" + wnumber(s.job->batch.size())) +
        L" | " + wnumber(elapsed) + L"s | " + activity;
  }
  line(height - 3, activity, 14);
  if (s.editing)
    line(height - 2, s.editLabel + s.edit + L"_", 15);
  else if (s.confirm) {
    size_t count = s.confirm->batch.size();
    std::wstring targets = s.confirm->args.empty() ? L"" : s.confirm->args[0];
    for (size_t i = 0; i < std::min<size_t>(count, 3); i++)
      targets += L" " + s.confirm->batch[i][2];
    if (count > 3)
      targets += L" +" + wnumber(count - 3) + L" more";
    line(height - 2, targets, 14);
  } else
    line(height - 2,
         L"Rows: " + wnumber(s.visible.size()) + L" | Selected: " +
             wnumber(std::count(s.checked.begin(), s.checked.end(), true)),
         8);
  SMALL_RECT region = info.srWindow;
  COORD size{width, height}, origin{0, 0};
  WriteConsoleOutputW(c.screen, frame.data(), size, origin, &region);
}
static bool keypress(State &s, const KEY_EVENT_RECORD &key) {
  if (!key.bKeyDown)
    return true;
  WORD vk = key.wVirtualKeyCode;
  wchar_t ch = key.uChar.UnicodeChar;
  bool ctrl = (key.dwControlKeyState &
               (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0,
       shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
  if (ctrl && vk == 'C') {
    if (s.worker && s.job->mutation) {
      s.status = L"Wait for the package operation before quitting.";
      return true;
    }
    cancelRead = true;
    s.pending.reset();
    return false;
  }
  if (s.editing) {
    if (vk == VK_ESCAPE) {
      s.editing = 0;
      return true;
    }
    if (ctrl && vk == 'A') {
      s.edit.clear();
      return true;
    }
    if (vk == VK_BACK) {
      if (!s.edit.empty())
        s.edit.pop_back();
      return true;
    }
    if (vk == VK_RETURN) {
      int mode = s.editing;
      s.editing = 0;
      if (mode == 1) {
        s.query = s.edit;
        if (s.view == REMOTE)
          s.load(false);
        else
          s.filter();
      } else if (!trim(s.edit).empty()) {
        auto next = transferJob(mode == 3, s.edit);
        if (mode == 3) {
          s.confirm = std::move(next);
          s.status = L"Confirm importing packages from " + s.edit + L": y / n";
        } else {
          s.start(std::move(next));
        }
      }
      return true;
    }
    if (ch >= 32)
      s.edit += ch;
    return true;
  }
  if (s.confirm) {
    if (ch == L'y' || ch == L'Y') {
      auto next = std::move(s.confirm);
      s.start(std::move(next));
    } else if (ch == L'n' || ch == L'N' || vk == VK_ESCAPE) {
      s.confirm.reset();
      s.status = L"Canceled";
    }
    return true;
  }
  if (ch == L'q' || ch == L'Q') {
    if (s.worker && s.job->mutation) {
      s.status = L"Wait for the package operation before quitting.";
      return true;
    }
    cancelRead = true;
    s.pending.reset();
    return false;
  }
  if (ch == L'l' || ch == L'L') {
    s.output = !s.output;
    return true;
  }
  if (vk == VK_ESCAPE && s.output) {
    s.output = false;
    return true;
  }
  if (s.output) {
    if (vk == VK_UP && s.logTop)
      s.logTop--;
    else if (vk == VK_DOWN)
      s.logTop++;
    else if (vk == VK_PRIOR)
      s.logTop = s.logTop > 15 ? s.logTop - 15 : 0;
    else if (vk == VK_NEXT)
      s.logTop += 15;
    return true;
  }
  if (ch >= L'1' && ch <= L'3') {
    s.view = ch == L'1' ? REMOTE : ch == L'2' ? INSTALLED : UPDATES;
    s.query.clear();
    s.load(false);
  } else if (ch == L'/') {
    s.editing = 1;
    s.edit = s.query;
    s.editLabel = L"Search/filter (Enter/Esc): ";
  } else if (ch == L'r' || ch == L'R')
    s.load(true);
  else if (ch == L'e' || ch == L'i') {
    if (s.worker) {
      s.status = L"Wait for background work before importing/exporting.";
      return true;
    }
    s.editing = ch == L'e' ? 2 : 3;
    s.edit = L"installed-packages.json";
    s.editLabel = L"JSON path (Ctrl+A clear, Enter/Esc): ";
  } else if (ctrl && vk == 'A') {
    for (size_t i : s.visible)
      s.checked[i] = true;
  } else if (vk == VK_RETURN)
    s.action();
  else if (vk == VK_SPACE)
    s.select(s.focus, shift);
  else if (!s.visible.empty()) {
    size_t old = s.focus;
    if (vk == VK_UP && s.focus)
      s.focus--;
    else if (vk == VK_DOWN)
      s.focus = std::min(s.focus + 1, s.visible.size() - 1);
    else if (vk == VK_PRIOR)
      s.focus = s.focus > 15 ? s.focus - 15 : 0;
    else if (vk == VK_NEXT)
      s.focus = std::min(s.focus + 15, s.visible.size() - 1);
    if (shift && s.focus != old)
      s.select(s.focus, true);
    else if (s.focus != old)
      s.anchor = s.focus;
  }
  return true;
}
#ifdef WINGETSWIFT_DIAGNOSTICS
#include "tui-tests.hpp"
#endif
int wmain(int argc, wchar_t **argv) {
  try {
#ifdef WINGETSWIFT_DIAGNOSTICS
    if (argc > 1 && !wcscmp(argv[1], L"--self-test"))
      return tui_tests();
#endif
    bool portable = false;
    for (int i = 1; i < argc; i++)
      if (!wcscmp(argv[i], L"--portable"))
        portable = true;
    initCache(portable);
    Console console;
    State state;
    bool active = true;
    while (active) {
      draw(console, state);
      HANDLE handles[2] = {console.input, state.worker};
      DWORD waited =
          WaitForMultipleObjects(state.worker ? 2 : 1, handles, FALSE, 100);
      if (waited == WAIT_OBJECT_0) {
        INPUT_RECORD records[32];
        DWORD count = 0;
        if (!ReadConsoleInputW(console.input, records, 32, &count))
          break;
        for (DWORD i = 0; i < count && active; i++)
          if (records[i].EventType == KEY_EVENT)
            active = keypress(state, records[i].Event.KeyEvent);
      } else if (waited == WAIT_OBJECT_0 + 1 && state.worker)
        state.complete();
      else if (waited == WAIT_FAILED)
        break;
    }
    if (state.worker) {
      cancelRead = true;
      WaitForSingleObject(state.worker, INFINITE);
      CloseHandle(state.worker);
      state.worker = nullptr;
    }
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "Winget Swift: %s\n", e.what());
    return 1;
  }
}
