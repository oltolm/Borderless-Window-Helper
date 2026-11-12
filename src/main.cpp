#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ini.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "resource.h"
#include "util.h"

namespace fs = std::filesystem;
constexpr wchar_t title[] = L"Borderless Window Helper";
constexpr UINT trayMessage = WM_APP + 1;
enum { monitoredId = 100, runningId, showId = 2000, startupId, menuId, exitId };
struct Less { bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; } };
struct Window { HWND handle; DWORD pid; fs::path path; std::wstring caption; HMONITOR monitor; bool borderless; };
struct Monitored { fs::path path; HWND observed = nullptr, modified = nullptr; LONG_PTR style = 0, exstyle = 0; bool active = false; };
static HINSTANCE instance;
static HWND mainWindow, leftList, rightList, leftTitle, rightTitle, details;
static HIMAGELIST images;
static HICON icon;
static fs::path config;
static std::map<std::wstring, Window, Less> windows;
static std::map<std::wstring, Monitored, Less> monitored;
static std::map<std::wstring, int, Less> imageIndex;
static bool updatingLists;

static std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), n, nullptr, nullptr);
    return result;
}
static std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    if (!n) { cp = CP_ACP; n = MultiByteToWideChar(cp, 0, value.data(), (int)value.size(), nullptr, 0); }
    std::wstring result(n, 0);
    MultiByteToWideChar(cp, 0, value.data(), (int)value.size(), result.data(), n);
    return result;
}
struct IniEntry { std::string path; };
static int parseEntry(void* context, const char* section, const char* key, const char* value) {
    char* end;
    long index = strtol(section, &end, 10);
    if (*section && !*end && index >= 0 && index < 100000 && !strcmp(key, "p"))
        (*static_cast<std::map<int, IniEntry>*>(context))[(int)index].path = value;
    return 1;
}
static void load() {
    FILE* file = _wfopen(config.c_str(), L"rb");
    if (!file) return;
    std::map<int, IniEntry> entries;
    int result = ini_parse_file(file, parseEntry, &entries);
    fclose(file);
    if (result) return;
    for (const auto& [number, entry] : entries) {
        if (entry.path.empty()) continue;
        fs::path path(wide(entry.path));
        std::wstring name = path.filename().wstring();
        if (name.empty()) continue;
        Monitored item;
        if (path.has_parent_path() && fs::exists(path)) item.path = path;
        monitored[name] = item;
    }
}
static void save() {
    std::error_code error;
    fs::create_directories(config.parent_path(), error);
    fs::path temp = config;
    temp += L".tmp";
    FILE* file = _wfopen(temp.c_str(), L"wb");
    if (!file) return;
    int i = 0;
    for (const auto& [name, item] : monitored) {
        std::string path = utf8(item.path.empty() ? name : item.path.wstring());
        fprintf(file, "[%d]\np = %s\ns = 0x%llx\nexstyle = 0x%llx\n\n", i++, path.c_str(),
                (unsigned long long)item.style, (unsigned long long)item.exstyle);
    }
    if (!fclose(file)) {
        if (!MoveFileExW(temp.c_str(), config.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(temp.c_str());
    } else DeleteFileW(temp.c_str());
}
static BOOL CALLBACK collect(HWND handle, LPARAM) {
    if (!IsWindowVisible(handle) || GetWindow(handle, GW_OWNER) || handle == mainWindow) return TRUE;
    std::wstring caption = GetWindowTextString(handle);
    if (caption.empty() || caption == L"Program Manager") return TRUE;
    DWORD pid;
    GetWindowThreadProcessId(handle, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    std::wstring pathText(32768, 0);
    DWORD size = (DWORD)pathText.size();
    BOOL ok = QueryFullProcessImageNameW(process, 0, pathText.data(), &size);
    CloseHandle(process);
    if (!ok) return TRUE;
    pathText.resize(size);
    fs::path path(pathText);
    if (path == AppPath()) return TRUE;
    std::wstring name = path.filename().wstring();
    if (windows.find(name) != windows.end()) return TRUE;
    LONG_PTR style = GetWindowLongPtrW(handle, GWL_STYLE);
    windows.emplace(name, Window{handle, pid, path, caption, MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST),
                                 !(style & (WS_CAPTION | WS_THICKFRAME))});
    return TRUE;
}
static void enumerate() { windows.clear(); EnumWindows(collect, 0); }
static void monitor() {
    HWND foreground = GetForegroundWindow();
    for (auto& [name, item] : monitored) {
        auto it = windows.find(name);
        if (it == windows.end()) { item.observed = nullptr; item.modified = nullptr; item.active = false; continue; }
        Window& win = it->second;
        if (item.observed != win.handle) {
            item.observed = win.handle;
            item.modified = nullptr;
            item.active = foreground == win.handle;
            if (!win.borderless) {
                item.style = GetWindowLongPtrW(win.handle, GWL_STYLE);
                item.exstyle = GetWindowLongPtrW(win.handle, GWL_EXSTYLE);
                MONITORINFO monitorInfo{sizeof(monitorInfo)};
                if (GetMonitorInfoW(win.monitor, &monitorInfo)) {
                    SetWindowLongPtrW(win.handle, GWL_STYLE, (item.style & ~(WS_CAPTION | WS_THICKFRAME)) | WS_POPUP);
                    SetWindowLongPtrW(win.handle, GWL_EXSTYLE, item.exstyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE));
                    RECT r = monitorInfo.rcMonitor;
                    SetWindowPos(win.handle, HWND_TOP, r.left, r.top, r.right-r.left, r.bottom-r.top,
                                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                    item.modified = win.handle;
                    win.borderless = true;
                }
            }
        }
        if (foreground == win.handle) item.active = true;
        else if (item.active && foreground && win.monitor == MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST)) {
            std::wstring className = GetClassNameString(foreground);
            if (className != L"TaskSwitcherWnd" && className != L"Ghost") {
                item.active = false;
                if (!IsIconic(win.handle)) PostMessageW(win.handle, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            }
        }
    }
}
static int imageFor(const fs::path& path) {
    if (path.empty()) return -1;
    auto found = imageIndex.find(path.wstring());
    if (found != imageIndex.end()) return found->second;
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON)) return -1;
    int index = ImageList_AddIcon(images, info.hIcon);
    DestroyIcon(info.hIcon);
    imageIndex[path.wstring()] = index;
    return index;
}
static std::wstring selected(HWND list) {
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0) return {};
    wchar_t text[MAX_PATH]{};
    ListView_GetItemText(list, row, 0, text, MAX_PATH);
    return text;
}
static void populate(HWND list, bool isMonitored) {
    updatingLists = true;
    std::wstring selection = selected(list);
    ListView_DeleteAllItems(list);
    int row = 0;
    if (isMonitored) {
        for (const auto& [name, item] : monitored) {
            LVITEMW entry{LVIF_TEXT | LVIF_IMAGE};
            entry.iItem = row++; entry.pszText = const_cast<wchar_t*>(name.c_str()); entry.iImage = imageFor(item.path);
            ListView_InsertItem(list, &entry);
        }
    } else {
        for (const auto& [name, item] : windows) {
            LVITEMW entry{LVIF_TEXT | LVIF_IMAGE};
            entry.iItem = row++; entry.pszText = const_cast<wchar_t*>(name.c_str()); entry.iImage = imageFor(item.path);
            ListView_InsertItem(list, &entry);
        }
    }
    if (!selection.empty()) {
        LVFINDINFOW find{LVFI_STRING}; find.psz = selection.c_str();
        int index = ListView_FindItem(list, -1, &find);
        if (index >= 0) ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    updatingLists = false;
}
static void showDetails() {
    std::wstring name = selected(rightList);
    auto running = windows.find(name);
    if (running != windows.end()) {
        const Window& win = running->second;
        wchar_t line[160];
        swprintf(line, 160, L"PID: %lu | Window handle: %p | Window %s\r\n\r\n", win.pid, win.handle,
                 win.borderless ? L"is borderless" : L"has a border");
        SetWindowTextW(details, (std::wstring(line) + L"Window title: " + win.caption).c_str());
        return;
    }
    name = selected(leftList);
    auto item = monitored.find(name);
    if (item != monitored.end()) {
        std::wstring text = windows.count(name) ? L"The process is running and being monitored." : L"The process is not running.";
        if (!item->second.path.empty()) text += L"\r\n\r\n" + item->second.path.wstring();
        SetWindowTextW(details, text.c_str());
        return;
    }
    SetWindowTextW(details, L"Select a process to see details.\r\nDouble-click a running process to monitor it.\r\nDouble-click a monitored process or press Delete to remove it.");
}
static void refresh() {
    enumerate(); monitor();
    if (IsWindowVisible(mainWindow)) { populate(leftList, true); populate(rightList, false); showDetails(); }
}
static void addSelected() {
    std::wstring name = selected(rightList);
    auto found = windows.find(name);
    if (found == windows.end() || monitored.count(name)) return;
    Monitored item; item.path = found->second.path;
    monitored[name] = item;
    monitor(); populate(leftList, true); showDetails(); save();
}
static void removeSelected() {
    std::vector<std::wstring> names;
    for (int row = -1; (row = ListView_GetNextItem(leftList, row, LVNI_SELECTED)) >= 0;) {
        wchar_t text[MAX_PATH]{}; ListView_GetItemText(leftList, row, 0, text, MAX_PATH); names.emplace_back(text);
    }
    for (const auto& name : names) {
        auto found = monitored.find(name);
        if (found == monitored.end()) continue;
        Monitored& item = found->second;
        if (item.modified && IsWindow(item.modified) && windows.count(name) && windows.at(name).handle == item.modified) {
            SetWindowLongPtrW(item.modified, GWL_STYLE, item.style);
            SetWindowLongPtrW(item.modified, GWL_EXSTYLE, item.exstyle);
            SetWindowPos(item.modified, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        monitored.erase(found);
    }
    if (!names.empty()) { populate(leftList, true); showDetails(); save(); }
}
static fs::path shortcut(bool startup) {
    fs::path path = GetSysFolderLocation(CSIDL_APPDATA);
    path /= L"Microsoft/Windows/Start Menu/Programs";
    if (startup) path /= L"Startup";
    return path / L"Borderless Window Helper.lnk";
}
static void toggleShortcut(bool startup) {
    fs::path path = shortcut(startup);
    if (fs::exists(path)) { std::error_code error; fs::remove(path, error); }
    else createShortcut(path, AppPath(), L"tray", L"");
}
static void show() { ShowWindow(mainWindow, SW_SHOW); SetForegroundWindow(mainWindow); refresh(); }
static void popup() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, showId, IsWindowVisible(mainWindow) ? L"Hide interface" : L"Show interface");
    AppendMenuW(menu, MF_STRING | (fs::exists(shortcut(true)) ? MF_CHECKED : 0), startupId, L"Start with Windows");
    AppendMenuW(menu, MF_STRING | (fs::exists(shortcut(false)) ? MF_CHECKED : 0), menuId, L"Start Menu");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, exitId, L"Exit");
    POINT point; GetCursorPos(&point); SetForegroundWindow(mainWindow);
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, mainWindow, nullptr);
    DestroyMenu(menu); PostMessageW(mainWindow, WM_NULL, 0, 0);
    if (choice == showId) { if (IsWindowVisible(mainWindow)) ShowWindow(mainWindow, SW_HIDE); else show(); }
    else if (choice == startupId) toggleShortcut(true);
    else if (choice == menuId) toggleShortcut(false);
    else if (choice == exitId) DestroyWindow(mainWindow);
}
static void layout(int width, int height) {
    if (!leftList) return;
    int column = (width - 45) / 2;
    int listHeight = height - 155;
    MoveWindow(leftTitle, 15, 15, column, 20, TRUE);
    MoveWindow(rightTitle, column + 30, 15, column, 20, TRUE);
    MoveWindow(leftList, 15, 35, column, listHeight, TRUE);
    MoveWindow(rightList, column + 30, 35, column, listHeight, TRUE);
    MoveWindow(details, 15, listHeight + 50, width - 30, 90, TRUE);
    ListView_SetColumnWidth(leftList, 0, column - 5);
    ListView_SetColumnWidth(rightList, 0, column - 5);
    RedrawWindow(leftTitle, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    RedrawWindow(rightTitle, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}
static HWND makeList(int id) {
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
        0, 0, 100, 100, mainWindow, (HMENU)(INT_PTR)id, instance, nullptr);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetImageList(list, images, LVSIL_SMALL);
    LVCOLUMNW column{LVCF_WIDTH}; column.cx = 100; ListView_InsertColumn(list, 0, &column);
    return list;
}
static LRESULT CALLBACK windowProc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        mainWindow = handle;
        images = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
        leftTitle = CreateWindowW(L"STATIC", L"Processes with windows being monitored:", WS_CHILD | WS_VISIBLE,
                                  0, 0, 0, 0, handle, nullptr, instance, nullptr);
        rightTitle = CreateWindowW(L"STATIC", L"Processes currently running with visible windows:", WS_CHILD | WS_VISIBLE,
                                   0, 0, 0, 0, handle, nullptr, instance, nullptr);
        leftList = makeList(monitoredId); rightList = makeList(runningId);
        details = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 0, 0, handle, nullptr, instance, nullptr);
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
            for (HWND control : {leftTitle, rightTitle, leftList, rightList, details})
                SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
            SetPropW(handle, L"BWH.Font", font);
        }
        NOTIFYICONDATAW tray{sizeof(tray)};
        tray.hWnd = handle; tray.uID = 1; tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray.hIcon = icon; tray.uCallbackMessage = trayMessage; wcscpy_s(tray.szTip, title);
        Shell_NotifyIconW(NIM_ADD, &tray);
        SetTimer(handle, 1, 1000, nullptr);
        refresh();
        return 0;
    }
    case WM_SIZE: layout(LOWORD(lparam), HIWORD(lparam)); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wparam;
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetBkMode(dc, OPAQUE);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_TIMER: if (wparam == 1) refresh(); return 0;
    case WM_NOTIFY: {
        auto* event = (NMHDR*)lparam;
        if (event->code == NM_DBLCLK) {
            if (event->idFrom == runningId) addSelected();
            else if (event->idFrom == monitoredId) removeSelected();
        } else if (event->code == LVN_ITEMCHANGED && !updatingLists) {
            auto* changed = (NMLISTVIEW*)lparam;
            if ((changed->uChanged & LVIF_STATE) && (changed->uNewState & LVIS_SELECTED) &&
                !(changed->uOldState & LVIS_SELECTED)) {
                HWND other = event->idFrom == monitoredId ? rightList : leftList;
                if (other) ListView_SetItemState(other, -1, 0, LVIS_SELECTED);
            }
            showDetails();
        }
        else if (event->code == LVN_KEYDOWN && event->idFrom == monitoredId &&
                 ((NMLVKEYDOWN*)lparam)->wVKey == VK_DELETE) removeSelected();
        return 0;
    }
    case WM_KEYUP: if (wparam == VK_ESCAPE) ShowWindow(handle, SW_HIDE); return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_CLOSE || (wparam & 0xfff0) == SC_MINIMIZE) {
            ShowWindow(handle, SW_HIDE); return 0;
        }
        break;
    case trayMessage:
        if (lparam == WM_LBUTTONDBLCLK) {
            if (IsWindowVisible(handle)) ShowWindow(handle, SW_HIDE); else show();
        } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) popup();
        return 0;
    case WM_ENDSESSION: if (wparam) save(); return 0;
    case WM_DESTROY: {
        KillTimer(handle, 1); save();
        NOTIFYICONDATAW tray{sizeof(tray)}; tray.hWnd = handle; tray.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &tray);
        ImageList_Destroy(images);
        if (HANDLE font = GetPropW(handle, L"BWH.Font")) DeleteObject(font);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}
int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, LPWSTR args, int showCommand) {
    instance = hinstance;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, title);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"An instance of the program is already running.", title, MB_ICONINFORMATION);
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    CoInitialize(nullptr);
    config = AppPath().parent_path() / L"bwh.ini";
    if (!fs::exists(config)) config = GetSysFolderLocation(CSIDL_APPDATA) / L"Borderless-Window-Helper/bwh.ini";
    load();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON));
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = windowProc; cls.hInstance = instance; cls.lpszClassName = title;
    cls.hIcon = icon; cls.hIconSm = icon; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&cls);
    RECT rect{0, 0, 640, 480}; AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND window = CreateWindowExW(0, title, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    if (!window) { CoUninitialize(); if (mutex) CloseHandle(mutex); return 1; }
    if (!args || !*args) ShowWindow(window, showCommand);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoUninitialize(); if (mutex) CloseHandle(mutex);
    return (int)message.wParam;
}
