// hidepro.cpp : Window Hider Program

#include "framework.h"
#include "hidepro.h"
#include <strsafe.h>

#define MAX_LOADSTRING 100
#define CONFIG_FILE L"hidepro.ini"

enum HideMethod {
    HIDE_SHOWWINDOW = 0,
    HIDE_SETWINDOWPOS,
    HIDE_MOVEOFFSCREEN,
    HIDE_TRANSPARENT,
    HIDE_STYLE,
    HIDE_COMBINED
};

struct WindowInfo {
    HWND hwnd;
    DWORD processId;
    std::wstring title;
    std::wstring processName;
    bool isHidden;
    bool isPending;
    HideMethod hideMethod;
    RECT originalRect;
    LONG originalStyle;
    LONG originalExStyle;
    BYTE originalAlpha;
};

HINSTANCE g_hInst;
HWND g_hMainDlg = NULL;
HWND g_hListView = NULL;
NOTIFYICONDATA g_nid = { 0 };
std::vector<WindowInfo> g_windowList;
bool g_isCapturing = false;
HCURSOR g_hCrossCursor = NULL;
UINT_PTR g_timerID = 0;
UINT_PTR g_findTimerID = 0;
HideMethod g_currentHideMethod = HIDE_COMBINED;

INT_PTR CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK ProcessDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK WindowDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AboutProc(HWND, UINT, WPARAM, LPARAM);

void InitListView(HWND hListView);
void RefreshListView();
void AddWindowToList(HWND hwnd);
void RemoveSelectedWindow();
void HideSelectedWindow();
void ShowSelectedWindow();
void HideAllWindows();
void ShowAllWindows();
void CreateTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowTrayMenu(HWND hWnd);
void SaveConfig();
void LoadConfig();
bool IsAutoRunEnabled();
void SetAutoRun(bool enable);
std::wstring GetProcessName(DWORD processId);
std::wstring GetConfigPath();
void ForceHideWindow(WindowInfo& wi);
void ForceShowWindow(WindowInfo& wi);
void CALLBACK WatchTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void CALLBACK FindWindowTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void TryFindPendingWindows();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    g_hInst = hInstance;

    HANDLE hMutex = CreateMutex(NULL, TRUE, L"HideProMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, L"HidePro is already running.", L"Info", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    g_hCrossCursor = LoadCursor(NULL, IDC_CROSS);

    LoadConfig();

    g_hMainDlg = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_MAINDLG), NULL, MainDlgProc, 0);
    if (!g_hMainDlg) {
        MessageBox(NULL, L"Dialog creation failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    CreateTrayIcon(g_hMainDlg);
    ShowWindow(g_hMainDlg, SW_HIDE);

    g_findTimerID = SetTimer(g_hMainDlg, 2, 3000, FindWindowTimerProc);

    TryFindPendingWindows();
    HideAllWindows();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(g_hMainDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    RemoveTrayIcon();
    SaveConfig();
    CloseHandle(hMutex);

    return (int)msg.wParam;
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        HICON hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_HIDEPRO));
        SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

        g_hListView = GetDlgItem(hDlg, IDC_LIST_WINDOWS);
        InitListView(g_hListView);

        CheckDlgButton(hDlg, IDC_CHK_AUTORUN, IsAutoRunEnabled() ? BST_CHECKED : BST_UNCHECKED);

        RefreshListView();

        return (INT_PTR)TRUE;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hDlg, SW_SHOW);
            SetForegroundWindow(hDlg);
        }
        else if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hDlg);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BTN_ADD_MOUSE:
            g_isCapturing = true;
            SetCapture(hDlg);
            SetCursor(g_hCrossCursor);
            SetDlgItemText(hDlg, IDC_STATIC_STATUS, L"Click window...");
            break;

        case IDC_BTN_ADD_PROCESS:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_PROCESSDLG), hDlg, ProcessDlgProc);
            break;

        case IDC_BTN_ADD_TITLE:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_WINDOWDLG), hDlg, WindowDlgProc);
            break;

        case IDC_BTN_REMOVE:
            RemoveSelectedWindow();
            break;

        case IDC_BTN_HIDE:
            HideSelectedWindow();
            break;

        case IDC_BTN_SHOW:
            ShowSelectedWindow();
            break;

        case IDC_BTN_HIDE_ALL:
            HideAllWindows();
            break;

        case IDC_BTN_SHOW_ALL:
            ShowAllWindows();
            break;

        case IDC_CHK_AUTORUN:
            SetAutoRun(IsDlgButtonChecked(hDlg, IDC_CHK_AUTORUN) == BST_CHECKED);
            break;

        case IDM_SHOW:
            ShowWindow(hDlg, SW_SHOW);
            SetForegroundWindow(hDlg);
            break;

        case IDM_HIDE_ALL:
            HideAllWindows();
            break;

        case IDM_SHOW_ALL:
            ShowAllWindows();
            break;

        case IDM_AUTORUN:
        {
            bool current = IsAutoRunEnabled();
            SetAutoRun(!current);
            CheckDlgButton(hDlg, IDC_CHK_AUTORUN, !current ? BST_CHECKED : BST_UNCHECKED);
        }
        break;

        case IDM_ABOUT:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, AboutProc);
            break;

        case IDM_EXIT:
            ShowAllWindows();
            DestroyWindow(hDlg);
            break;

        case IDOK:
            ShowWindow(hDlg, SW_HIDE);
            break;

        case IDCANCEL:
            ShowAllWindows();
            DestroyWindow(hDlg);
            break;
        }
    }
    return (INT_PTR)TRUE;

    case WM_LBUTTONUP:
        if (g_isCapturing) {
            g_isCapturing = false;
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            SetDlgItemText(hDlg, IDC_STATIC_STATUS, L"");

            POINT pt;
            GetCursorPos(&pt);
            HWND hwndTarget = WindowFromPoint(pt);

            HWND hwndRoot = GetAncestor(hwndTarget, GA_ROOT);
            if (hwndRoot && hwndRoot != hDlg) {
                AddWindowToList(hwndRoot);
            }
        }
        return (INT_PTR)TRUE;

    case WM_MOUSEMOVE:
        if (g_isCapturing) {
            SetCursor(g_hCrossCursor);
        }
        return (INT_PTR)TRUE;

    case WM_SYSCOMMAND:
        if (wParam == SC_MINIMIZE) {
            ShowWindow(hDlg, SW_HIDE);
            return (INT_PTR)TRUE;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hDlg, SW_HIDE);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        if (g_timerID != 0) {
            KillTimer(hDlg, g_timerID);
            g_timerID = 0;
        }
        if (g_findTimerID != 0) {
            KillTimer(hDlg, g_findTimerID);
            g_findTimerID = 0;
        }
        SaveConfig();
        PostQuitMessage(0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void InitListView(HWND hListView)
{
    ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN lvc = { 0 };
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    lvc.fmt = LVCFMT_LEFT;

    lvc.cx = 200;
    lvc.pszText = (LPWSTR)L"Title";
    ListView_InsertColumn(hListView, 0, &lvc);

    lvc.cx = 120;
    lvc.pszText = (LPWSTR)L"Process";
    ListView_InsertColumn(hListView, 1, &lvc);

    lvc.cx = 60;
    lvc.pszText = (LPWSTR)L"Status";
    ListView_InsertColumn(hListView, 2, &lvc);
}

void RefreshListView()
{
    ListView_DeleteAllItems(g_hListView);

    LVITEM lvi = { 0 };
    lvi.mask = LVIF_TEXT;

    for (size_t i = 0; i < g_windowList.size(); i++) {
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)g_windowList[i].title.c_str();
        ListView_InsertItem(g_hListView, &lvi);

        ListView_SetItemText(g_hListView, (int)i, 1, (LPWSTR)g_windowList[i].processName.c_str());
        
        LPCWSTR status;
        if (g_windowList[i].isPending) {
            status = L"Waiting";
        } else if (g_windowList[i].isHidden) {
            status = L"Hidden";
        } else {
            status = L"Visible";
        }
        ListView_SetItemText(g_hListView, (int)i, 2, (LPWSTR)status);
    }
}

void AddWindowToList(HWND hwnd)
{
    if (!IsWindow(hwnd)) return;

    WCHAR title[256] = { 0 };
    GetWindowText(hwnd, title, 256);
    
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    std::wstring procName = GetProcessName(processId);

    for (size_t i = 0; i < g_windowList.size(); i++) {
        if (g_windowList[i].processName == procName && g_windowList[i].title == title) {
            MessageBox(g_hMainDlg, L"Already registered.", L"Info", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    WindowInfo wi;
    memset(&wi, 0, sizeof(wi));
    wi.hwnd = hwnd;
    wi.processId = processId;
    wi.title = title;
    wi.processName = procName;
    wi.isHidden = false;
    wi.isPending = false;
    wi.hideMethod = HIDE_COMBINED;

    GetWindowRect(hwnd, &wi.originalRect);
    wi.originalStyle = GetWindowLong(hwnd, GWL_STYLE);
    wi.originalExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    wi.originalAlpha = 255;

    g_windowList.push_back(wi);
    RefreshListView();
    SaveConfig();
}

void RemoveSelectedWindow()
{
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)g_windowList.size()) {
        if (!g_windowList[sel].isPending && g_windowList[sel].isHidden && IsWindow(g_windowList[sel].hwnd)) {
            ForceShowWindow(g_windowList[sel]);
        }
        g_windowList.erase(g_windowList.begin() + sel);
        RefreshListView();
        SaveConfig();
    }
}

void ForceHideWindow(WindowInfo& wi)
{
    if (wi.isPending || !IsWindow(wi.hwnd)) return;

    GetWindowRect(wi.hwnd, &wi.originalRect);
    wi.originalStyle = GetWindowLong(wi.hwnd, GWL_STYLE);
    wi.originalExStyle = GetWindowLong(wi.hwnd, GWL_EXSTYLE);

    BYTE alpha = 255;
    DWORD flags = 0;
    if (GetLayeredWindowAttributes(wi.hwnd, NULL, &alpha, &flags)) {
        wi.originalAlpha = alpha;
    }
    else {
        wi.originalAlpha = 255;
    }

    wi.hideMethod = g_currentHideMethod;

    ShowWindow(wi.hwnd, SW_HIDE);
    SetWindowPos(wi.hwnd, NULL, 0, 0, 0, 0,
        SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(wi.hwnd, NULL, -32000, -32000, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowLong(wi.hwnd, GWL_EXSTYLE, GetWindowLong(wi.hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(wi.hwnd, 0, 0, LWA_ALPHA);
    SetWindowLong(wi.hwnd, GWL_STYLE, GetWindowLong(wi.hwnd, GWL_STYLE) & ~WS_VISIBLE);
    SetWindowPos(wi.hwnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    wi.isHidden = true;
}

void ForceShowWindow(WindowInfo& wi)
{
    if (wi.isPending || !IsWindow(wi.hwnd)) return;

    SetWindowLong(wi.hwnd, GWL_STYLE, wi.originalStyle);
    SetWindowLong(wi.hwnd, GWL_EXSTYLE, wi.originalExStyle);

    SetWindowPos(wi.hwnd, NULL,
        wi.originalRect.left, wi.originalRect.top,
        wi.originalRect.right - wi.originalRect.left,
        wi.originalRect.bottom - wi.originalRect.top,
        SWP_NOZORDER | SWP_FRAMECHANGED);

    if (wi.originalExStyle & WS_EX_LAYERED) {
        SetLayeredWindowAttributes(wi.hwnd, 0, wi.originalAlpha, LWA_ALPHA);
    }

    ShowWindow(wi.hwnd, SW_SHOW);
    SetWindowPos(wi.hwnd, NULL, 0, 0, 0, 0,
        SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    wi.isHidden = false;
}

struct FindWindowData {
    std::wstring processName;
    std::wstring title;
    HWND foundHwnd;
};

BOOL CALLBACK FindWindowByInfoCallback(HWND hwnd, LPARAM lParam)
{
    FindWindowData* data = (FindWindowData*)lParam;

    WCHAR title[256] = { 0 };
    GetWindowText(hwnd, title, 256);
    if (wcslen(title) == 0) return TRUE;

    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    std::wstring procName = GetProcessName(processId);

    if (procName == data->processName && std::wstring(title) == data->title) {
        data->foundHwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

void TryFindPendingWindows()
{
    bool found = false;
    
    for (size_t i = 0; i < g_windowList.size(); i++) {
        if (g_windowList[i].isPending) {
            FindWindowData data;
            data.processName = g_windowList[i].processName;
            data.title = g_windowList[i].title;
            data.foundHwnd = NULL;
            EnumWindows(FindWindowByInfoCallback, (LPARAM)&data);

            if (data.foundHwnd) {
                g_windowList[i].hwnd = data.foundHwnd;
                g_windowList[i].isPending = false;
                GetWindowThreadProcessId(data.foundHwnd, &g_windowList[i].processId);
                GetWindowRect(data.foundHwnd, &g_windowList[i].originalRect);
                g_windowList[i].originalStyle = GetWindowLong(data.foundHwnd, GWL_STYLE);
                g_windowList[i].originalExStyle = GetWindowLong(data.foundHwnd, GWL_EXSTYLE);
                g_windowList[i].originalAlpha = 255;
                
                ForceHideWindow(g_windowList[i]);
                found = true;
            }
        }
    }
    
    if (found) {
        RefreshListView();
    }
}

void CALLBACK FindWindowTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(idEvent);
    UNREFERENCED_PARAMETER(dwTime);

    TryFindPendingWindows();
}

void CALLBACK WatchTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(idEvent);
    UNREFERENCED_PARAMETER(dwTime);

    for (size_t i = 0; i < g_windowList.size(); i++) {
        if (!g_windowList[i].isPending && g_windowList[i].isHidden && IsWindow(g_windowList[i].hwnd)) {
            if (IsWindowVisible(g_windowList[i].hwnd)) {
                ForceHideWindow(g_windowList[i]);
            }
        }
    }
}

void HideSelectedWindow()
{
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)g_windowList.size()) {
        if (!g_windowList[sel].isPending && IsWindow(g_windowList[sel].hwnd)) {
            ForceHideWindow(g_windowList[sel]);
            RefreshListView();
        }
    }
}

void ShowSelectedWindow()
{
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)g_windowList.size()) {
        if (!g_windowList[sel].isPending && IsWindow(g_windowList[sel].hwnd)) {
            ForceShowWindow(g_windowList[sel]);
            RefreshListView();
        }
    }
}

void HideAllWindows()
{
    for (size_t i = 0; i < g_windowList.size(); i++) {
        if (!g_windowList[i].isPending && IsWindow(g_windowList[i].hwnd)) {
            ForceHideWindow(g_windowList[i]);
        }
    }
    RefreshListView();

    if (g_timerID == 0 && g_hMainDlg) {
        g_timerID = SetTimer(g_hMainDlg, 1, 500, WatchTimerProc);
    }
}

void ShowAllWindows()
{
    if (g_timerID != 0 && g_hMainDlg) {
        KillTimer(g_hMainDlg, g_timerID);
        g_timerID = 0;
    }

    for (size_t i = 0; i < g_windowList.size(); i++) {
        if (!g_windowList[i].isPending && IsWindow(g_windowList[i].hwnd)) {
            ForceShowWindow(g_windowList[i]);
        }
    }
    RefreshListView();
}

void CreateTrayIcon(HWND hWnd)
{
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd;
    g_nid.uID = ID_TRAYICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_HIDEPRO));
    StringCchCopy(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"HidePro");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDM_SHOW, L"Open");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDM_HIDE_ALL, L"Hide All");
    AppendMenu(hMenu, MF_STRING, IDM_SHOW_ALL, L"Show All");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING | (IsAutoRunEnabled() ? MF_CHECKED : 0), IDM_AUTORUN, L"Auto Start");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

std::wstring GetProcessName(DWORD processId)
{
    std::wstring name = L"Unknown";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        WCHAR path[MAX_PATH] = { 0 };
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageName(hProcess, 0, path, &size)) {
            WCHAR* fileName = wcsrchr(path, L'\\');
            if (fileName) {
                name = fileName + 1;
            }
        }
        CloseHandle(hProcess);
    }
    return name;
}

std::wstring GetConfigPath()
{
    WCHAR path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }
    wcscat_s(path, CONFIG_FILE);
    return path;
}

void SaveConfig()
{
    std::wstring configPath = GetConfigPath();
    FILE* fp = nullptr;
    _wfopen_s(&fp, configPath.c_str(), L"w,ccs=UTF-8");
    if (!fp) return;

    for (size_t i = 0; i < g_windowList.size(); i++) {
        fwprintf(fp, L"%s|%s\n", g_windowList[i].processName.c_str(), g_windowList[i].title.c_str());
    }
    fclose(fp);
}

void LoadConfig()
{
    std::wstring configPath = GetConfigPath();
    FILE* fp = nullptr;
    _wfopen_s(&fp, configPath.c_str(), L"r,ccs=UTF-8");
    if (!fp) return;

    WCHAR line[512];
    while (fgetws(line, 512, fp)) {
        size_t len = wcslen(line);
        if (len > 0 && line[len - 1] == L'\n') line[len - 1] = L'\0';
        len = wcslen(line);
        if (len > 0 && line[len - 1] == L'\r') line[len - 1] = L'\0';
        
        if (wcslen(line) == 0) continue;

        WCHAR* sep = wcschr(line, L'|');
        if (sep) {
            *sep = L'\0';
            std::wstring processName = line;
            std::wstring title = sep + 1;

            if (processName.empty() || title.empty()) continue;

            WindowInfo wi;
            memset(&wi, 0, sizeof(wi));
            wi.hwnd = NULL;
            wi.processId = 0;
            wi.title = title;
            wi.processName = processName;
            wi.isHidden = false;
            wi.isPending = true;
            wi.hideMethod = HIDE_COMBINED;
            wi.originalAlpha = 255;
            
            g_windowList.push_back(wi);
        }
    }
    fclose(fp);
}

bool IsAutoRunEnabled()
{
    bool result = false;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
        IID_ITaskService, (void**)&pService);

    if (SUCCEEDED(hr)) {
        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (SUCCEEDED(hr)) {
            ITaskFolder* pFolder = NULL;
            hr = pService->GetFolder(_bstr_t(L"\\"), &pFolder);
            if (SUCCEEDED(hr)) {
                IRegisteredTask* pTask = NULL;
                hr = pFolder->GetTask(_bstr_t(L"HidePro"), &pTask);
                if (SUCCEEDED(hr)) {
                    result = true;
                    pTask->Release();
                }
                pFolder->Release();
            }
        }
        pService->Release();
    }
    CoUninitialize();
    return result;
}

void SetAutoRun(bool enable)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
        IID_ITaskService, (void**)&pService);

    if (FAILED(hr)) {
        CoUninitialize();
        return;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        CoUninitialize();
        return;
    }

    ITaskFolder* pFolder = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pFolder);
    if (FAILED(hr)) {
        pService->Release();
        CoUninitialize();
        return;
    }

    if (enable) {
        pFolder->DeleteTask(_bstr_t(L"HidePro"), 0);

        ITaskDefinition* pTask = NULL;
        hr = pService->NewTask(0, &pTask);
        if (SUCCEEDED(hr)) {
            IRegistrationInfo* pRegInfo = NULL;
            hr = pTask->get_RegistrationInfo(&pRegInfo);
            if (SUCCEEDED(hr)) {
                pRegInfo->put_Author(_bstr_t(L"HidePro"));
                pRegInfo->put_Description(_bstr_t(L"HidePro Window Hider"));
                pRegInfo->Release();
            }

            IPrincipal* pPrincipal = NULL;
            hr = pTask->get_Principal(&pPrincipal);
            if (SUCCEEDED(hr)) {
                pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
                pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
                pPrincipal->Release();
            }

            ITriggerCollection* pTriggers = NULL;
            hr = pTask->get_Triggers(&pTriggers);
            if (SUCCEEDED(hr)) {
                ITrigger* pTrigger = NULL;
                hr = pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger);
                if (SUCCEEDED(hr)) {
                    ILogonTrigger* pLogonTrigger = NULL;
                    hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
                    if (SUCCEEDED(hr)) {
                        pLogonTrigger->put_Id(_bstr_t(L"LogonTriggerId"));
                        pLogonTrigger->put_Delay(_bstr_t(L"PT10S"));
                        pLogonTrigger->Release();
                    }
                    pTrigger->Release();
                }
                pTriggers->Release();
            }

            IActionCollection* pActions = NULL;
            hr = pTask->get_Actions(&pActions);
            if (SUCCEEDED(hr)) {
                IAction* pAction = NULL;
                hr = pActions->Create(TASK_ACTION_EXEC, &pAction);
                if (SUCCEEDED(hr)) {
                    IExecAction* pExecAction = NULL;
                    hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
                    if (SUCCEEDED(hr)) {
                        WCHAR path[MAX_PATH];
                        GetModuleFileName(NULL, path, MAX_PATH);
                        pExecAction->put_Path(_bstr_t(path));
                        pExecAction->Release();
                    }
                    pAction->Release();
                }
                pActions->Release();
            }

            ITaskSettings* pSettings = NULL;
            hr = pTask->get_Settings(&pSettings);
            if (SUCCEEDED(hr)) {
                pSettings->put_StartWhenAvailable(VARIANT_TRUE);
                pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
                pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
                pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
                pSettings->Release();
            }

            IRegisteredTask* pRegisteredTask = NULL;
            hr = pFolder->RegisterTaskDefinition(
                _bstr_t(L"HidePro"),
                pTask,
                TASK_CREATE_OR_UPDATE,
                _variant_t(),
                _variant_t(),
                TASK_LOGON_INTERACTIVE_TOKEN,
                _variant_t(L""),
                &pRegisteredTask);

            if (SUCCEEDED(hr)) {
                pRegisteredTask->Release();
            }
            pTask->Release();
        }
    }
    else {
        pFolder->DeleteTask(_bstr_t(L"HidePro"), 0);
    }

    pFolder->Release();
    pService->Release();
    CoUninitialize();
}

struct ProcessWindowInfo {
    HWND hwnd;
    std::wstring title;
    std::wstring processName;
    DWORD processId;
};

std::vector<ProcessWindowInfo> g_processWindows;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;

    WCHAR title[256] = { 0 };
    GetWindowText(hwnd, title, 256);
    if (wcslen(title) == 0) return TRUE;

    if (hwnd == g_hMainDlg) return TRUE;

    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    ProcessWindowInfo pwi;
    pwi.hwnd = hwnd;
    pwi.title = title;
    pwi.processId = processId;
    pwi.processName = GetProcessName(processId);

    g_processWindows.push_back(pwi);
    return TRUE;
}

INT_PTR CALLBACK ProcessDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hListView = NULL;

    switch (message)
    {
    case WM_INITDIALOG:
    {
        hListView = GetDlgItem(hDlg, IDC_LIST_PROCESS);
        ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;

        lvc.cx = 150;
        lvc.pszText = (LPWSTR)L"Title";
        ListView_InsertColumn(hListView, 0, &lvc);

        lvc.cx = 100;
        lvc.pszText = (LPWSTR)L"Process";
        ListView_InsertColumn(hListView, 1, &lvc);

        SendMessage(hDlg, WM_COMMAND, IDC_BTN_REFRESH, 0);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_REFRESH:
        {
            ListView_DeleteAllItems(hListView);
            g_processWindows.clear();
            EnumWindows(EnumWindowsProc, 0);

            LVITEM lvi = { 0 };
            lvi.mask = LVIF_TEXT;

            for (size_t i = 0; i < g_processWindows.size(); i++) {
                lvi.iItem = (int)i;
                lvi.iSubItem = 0;
                lvi.pszText = (LPWSTR)g_processWindows[i].title.c_str();
                ListView_InsertItem(hListView, &lvi);
                ListView_SetItemText(hListView, (int)i, 1, (LPWSTR)g_processWindows[i].processName.c_str());
            }
        }
        break;

        case IDOK:
        {
            int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_processWindows.size()) {
                AddWindowToList(g_processWindows[sel].hwnd);
            }
            EndDialog(hDlg, IDOK);
        }
        break;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return (INT_PTR)TRUE;

    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->code == NM_DBLCLK && ((LPNMHDR)lParam)->idFrom == IDC_LIST_PROCESS) {
            SendMessage(hDlg, WM_COMMAND, IDOK, 0);
        }
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

std::vector<ProcessWindowInfo> g_allWindows;

BOOL CALLBACK EnumAllWindowsProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    WCHAR title[256] = { 0 };
    GetWindowText(hwnd, title, 256);
    if (wcslen(title) == 0) return TRUE;

    if (hwnd == g_hMainDlg) return TRUE;

    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    ProcessWindowInfo pwi;
    pwi.hwnd = hwnd;
    pwi.title = title;
    pwi.processId = processId;
    pwi.processName = GetProcessName(processId);

    g_allWindows.push_back(pwi);
    return TRUE;
}

INT_PTR CALLBACK WindowDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hListView = NULL;
    static HWND hEdit = NULL;

    switch (message)
    {
    case WM_INITDIALOG:
    {
        hListView = GetDlgItem(hDlg, IDC_LIST_ALLWINDOWS);
        hEdit = GetDlgItem(hDlg, IDC_EDIT_FILTER);

        ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;

        lvc.cx = 200;
        lvc.pszText = (LPWSTR)L"Title";
        ListView_InsertColumn(hListView, 0, &lvc);

        lvc.cx = 100;
        lvc.pszText = (LPWSTR)L"Process";
        ListView_InsertColumn(hListView, 1, &lvc);

        SendMessage(hDlg, WM_COMMAND, IDC_BTN_FILTER, 0);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_FILTER:
        {
            WCHAR filter[256] = { 0 };
            GetWindowText(hEdit, filter, 256);
            std::wstring filterStr = filter;
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::towlower);

            ListView_DeleteAllItems(hListView);
            g_allWindows.clear();
            EnumWindows(EnumAllWindowsProc, 0);

            LVITEM lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            int itemIndex = 0;

            for (size_t i = 0; i < g_allWindows.size(); i++) {
                if (!filterStr.empty()) {
                    std::wstring titleLower = g_allWindows[i].title;
                    std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::towlower);
                    std::wstring procLower = g_allWindows[i].processName;
                    std::transform(procLower.begin(), procLower.end(), procLower.begin(), ::towlower);

                    if (titleLower.find(filterStr) == std::wstring::npos &&
                        procLower.find(filterStr) == std::wstring::npos) {
                        continue;
                    }
                }

                lvi.iItem = itemIndex;
                lvi.iSubItem = 0;
                lvi.pszText = (LPWSTR)g_allWindows[i].title.c_str();
                lvi.lParam = (LPARAM)i;
                ListView_InsertItem(hListView, &lvi);
                ListView_SetItemText(hListView, itemIndex, 1, (LPWSTR)g_allWindows[i].processName.c_str());
                itemIndex++;
            }
        }
        break;

        case IDOK:
        {
            int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
            if (sel >= 0) {
                LVITEM lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = sel;
                ListView_GetItem(hListView, &lvi);
                size_t idx = (size_t)lvi.lParam;
                if (idx < g_allWindows.size()) {
                    AddWindowToList(g_allWindows[idx].hwnd);
                }
            }
            EndDialog(hDlg, IDOK);
        }
        break;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return (INT_PTR)TRUE;

    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->code == NM_DBLCLK && ((LPNMHDR)lParam)->idFrom == IDC_LIST_ALLWINDOWS) {
            SendMessage(hDlg, WM_COMMAND, IDOK, 0);
        }
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
