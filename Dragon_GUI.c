/* ================================================================
   Dragon Builder GUI v1.0  —  Win32 Frontend
   Simple, clean, single-window interface.
   Compile: gcc Dragon_GUI.c -o Dragon_GUI.exe -mwindows -lcomdlg32 -lole32 -lshell32
   ================================================================ */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>

#define APP_NAME     L"Dragon Builder"
#define WIN_W        720
#define WIN_H        520
#define PAD          20
#define BTN_H        28
#define EDIT_H       24

/* Control IDs */
#define IDC_DIR      100
#define IDC_BROWSE   101
#define IDC_ENTRY    102
#define IDC_BUILD    103
#define IDC_LOG      104
#define IDC_STATUS   105

/* Accent colour for the Build button (nice blue) */
#define ACCENT_R     0
#define ACCENT_G     120
#define ACCENT_B     212

static HINSTANCE g_hInst;
static HFONT g_fontMain;
static HFONT g_fontMono;
static HWND  hDir, hEntry, hLog, hStatus, hBuild, hBrowse;
static HWND  hLblDir, hLblEntry;

/* --------------------------------------------------------------- */
static void SetSegoeFont(HWND hWnd, int size, BOOL bold) {
    HDC hdc = GetDC(hWnd);
    int px = -MulDiv(size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(hWnd, hdc);
    HFONT hf = CreateFontW(px, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH,
        L"Segoe UI");
    SendMessageW(hWnd, WM_SETFONT, (WPARAM)hf, TRUE);
}

static void AppendLog(const wchar_t *text) {
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, len, len);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)text);
}

static void AppendLogA(const char *text) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wbuf = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    if (!wbuf) return;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, wlen);
    AppendLog(wbuf);
    free(wbuf);
}

static void SetStatus(const wchar_t *text) {
    SetWindowTextW(hStatus, text);
}

/* --------------------------------------------------------------- */
static BOOL BrowseForFolder(HWND owner, wchar_t *out, DWORD outLen) {
    IFileDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL,
        CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void**)&pfd);
    if (FAILED(hr)) {
        /* Fallback: old-style SHBrowseForFolder */
        BROWSEINFOW bi = {0};
        bi.hwndOwner = owner;
        bi.lpszTitle = L"Select your Python project folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return FALSE;
        BOOL ok = SHGetPathFromIDListW(pidl, out);
        CoTaskMemFree(pidl);
        return ok;
    }

    DWORD opts;
    pfd->lpVtbl->GetOptions(pfd, &opts);
    pfd->lpVtbl->SetOptions(pfd, opts | FOS_PICKFOLDERS);
    pfd->lpVtbl->SetTitle(pfd, L"Select your Python project folder");

    hr = pfd->lpVtbl->Show(pfd, owner);
    if (SUCCEEDED(hr)) {
        IShellItem *psi = NULL;
        hr = pfd->lpVtbl->GetResult(pfd, &psi);
        if (SUCCEEDED(hr)) {
            PWSTR path = NULL;
            psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &path);
            if (path) {
                wcsncpy(out, path, outLen-1);
                out[outLen-1] = L'\0';
                CoTaskMemFree(path);
            }
            psi->lpVtbl->Release(psi);
        }
    }
    pfd->lpVtbl->Release(pfd);
    return SUCCEEDED(hr) && out[0];
}

/* --------------------------------------------------------------- */
static DWORD WINAPI BuildThread(LPVOID lpParam) {
    (void)lpParam;

    wchar_t wdir[512], wentry[128];
    GetWindowTextW(hDir, wdir, 512);
    GetWindowTextW(hEntry, wentry, 128);

    /* Validation */
    if (wdir[0] == L'\0') {
        SetStatus(L"[!] Please select a project folder.");
        EnableWindow(hBuild, TRUE);
        return 1;
    }
    if (wentry[0] == L'\0') {
        SetStatus(L"[!] Please enter the main entry file (e.g. main.py).");
        EnableWindow(hBuild, TRUE);
        return 1;
    }

    /* Convert inputs to UTF-8 for Dragon */
    char dirA[512], entryA[128];
    WideCharToMultiByte(CP_UTF8, 0, wdir, -1, dirA, 512, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, wentry, -1, entryA, 128, NULL, NULL);

    /* Get Dragon EXE path (same directory as this GUI) */
    wchar_t guiPath[MAX_PATH];
    GetModuleFileNameW(NULL, guiPath, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(guiPath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    wchar_t dragonExe[MAX_PATH];
    _snwprintf(dragonExe, MAX_PATH, L"%sDragon_1.5.exe", guiPath);

    /* Verify Dragon exists */
    DWORD attribs = GetFileAttributesW(dragonExe);
    if (attribs == INVALID_FILE_ATTRIBUTES) {
        AppendLogA("[!] Dragon_1.5.exe not found. Please place it in the same folder as this GUI.\r\n");
        SetStatus(L"[!] Dragon_1.5.exe missing.");
        EnableWindow(hBuild, TRUE);
        return 1;
    }

    AppendLogA("========================================\r\n");
    AppendLogA("[*] Starting Dragon Builder...\r\n");
    AppendLogA("[*] Project: "); AppendLogA(dirA); AppendLogA("\r\n");
    AppendLogA("[*] Entry:   "); AppendLogA(entryA); AppendLogA("\r\n\r\n");
    SetStatus(L"[*] Building... please wait.");

    /* Create pipes for stdin, stdout, stderr */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hStdinRead, hStdinWrite;
    HANDLE hStdoutRead, hStdoutWrite;
    CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 65536);

    /* Prepare input: projectName (dirname) + newline + entryName + newline */
    /* Extract basename of dirA as project name */
    char *projName = strrchr(dirA, '\\');
    if (!projName) projName = dirA;
    else projName++;

    char inputBuf[1024];
    snprintf(inputBuf, sizeof(inputBuf), "%s\n%s\n", projName, entryA);
    DWORD written;
    WriteFile(hStdinWrite, inputBuf, (DWORD)strlen(inputBuf), &written, NULL);
    CloseHandle(hStdinWrite);

    /* Launch Dragon */
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput  = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError  = hStdoutWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};
    wchar_t cmdLine[MAX_PATH * 2];
    _snwprintf(cmdLine, MAX_PATH*2, L"\"%s\"", dragonExe);

    BOOL created = CreateProcessW(dragonExe, cmdLine, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    if (!created) {
        AppendLogA("[!] Failed to launch Dragon_1.5.exe\r\n");
        SetStatus(L"[!] Launch failed.");
        CloseHandle(hStdoutRead);
        EnableWindow(hBuild, TRUE);
        return 1;
    }

    /* Read output */
    char outBuf[4096];
    DWORD read;
    while (ReadFile(hStdoutRead, outBuf, sizeof(outBuf)-1, &read, NULL) && read > 0) {
        outBuf[read] = '\0';
        AppendLogA(outBuf);
    }

    /* Wait for completion */
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdoutRead);

    AppendLogA("\r\n");
    if (exitCode == 0) {
        AppendLogA("[+] Build completed successfully.\r\n");
        SetStatus(L"[+] Build completed successfully.");
    } else {
        AppendLogA("[!] Build failed.\r\n");
        SetStatus(L"[!] Build failed.");
    }
    AppendLogA("========================================\r\n\r\n");

    EnableWindow(hBuild, TRUE);
    return 0;
}

/* --------------------------------------------------------------- */
static void OnBuild(void) {
    EnableWindow(hBuild, FALSE);
    SetStatus(L"[*] Building...");
    CreateThread(NULL, 0, BuildThread, NULL, 0, NULL);
}

/* --------------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        /* Background brush for soft grey */
        HBRUSH hbrBg = CreateSolidBrush(RGB(245, 246, 247));
        SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)hbrBg);

        /* Labels */
        hLblDir = CreateWindowExW(0, L"STATIC", L"Project folder",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            PAD, PAD, 200, 18, hWnd, NULL, g_hInst, NULL);
        SetSegoeFont(hLblDir, 10, TRUE);

        hLblEntry = CreateWindowExW(0, L"STATIC", L"Main entry file (e.g. main.py)",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            PAD, PAD + 48, 240, 18, hWnd, NULL, g_hInst, NULL);
        SetSegoeFont(hLblEntry, 10, TRUE);

        /* Directory text box */
        hDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            PAD, PAD + 20, WIN_W - PAD*3 - 80, EDIT_H,
            hWnd, (HMENU)IDC_DIR, g_hInst, NULL);
        SetSegoeFont(hDir, 10, FALSE);
        SendMessageW(hDir, EM_SETCUEBANNER, TRUE, (LPARAM)L"C:\\path\\to\\my_project");

        /* Browse button */
        hBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
            WIN_W - PAD - 90, PAD + 19, 90, BTN_H + 2,
            hWnd, (HMENU)IDC_BROWSE, g_hInst, NULL);
        SetSegoeFont(hBrowse, 10, FALSE);

        /* Entry file text box */
        hEntry = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            PAD, PAD + 68, WIN_W - PAD*2, EDIT_H,
            hWnd, (HMENU)IDC_ENTRY, g_hInst, NULL);
        SetSegoeFont(hEntry, 10, FALSE);
        SendMessageW(hEntry, EM_SETCUEBANNER, TRUE, (LPARAM)L"main.py");

        /* Build button (accent) */
        hBuild = CreateWindowExW(0, L"BUTTON", L"  Build",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_OWNERDRAW,
            PAD, PAD + 108, 120, BTN_H + 4,
            hWnd, (HMENU)IDC_BUILD, g_hInst, NULL);
        SetSegoeFont(hBuild, 10, TRUE);

        /* Log area (monospaced) */
        hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            PAD, PAD + 152, WIN_W - PAD*2, WIN_H - PAD*2 - 180,
            hWnd, (HMENU)IDC_LOG, g_hInst, NULL);
        SendMessageW(hLog, EM_SETLIMITTEXT, 0, 0); /* unlimited */
        SetSegoeFont(hLog, 9, FALSE);
        SendMessageW(hLog, WM_SETFONT, (WPARAM)g_fontMono, TRUE);

        /* Set dark-grey background for log */
        HBRUSH hbrLog = CreateSolidBrush(RGB(30, 30, 30));
        /* We can't easily change edit bg colour without owner-draw, but 
           setting text colour works well enough with default white-on-grey. */

        /* Status bar at bottom */
        hStatus = CreateWindowExW(0, L"STATIC", L" Ready",
            WS_VISIBLE | WS_CHILD | SS_LEFT | SS_SUNKEN,
            0, WIN_H - 24, WIN_W, 24,
            hWnd, (HMENU)IDC_STATUS, g_hInst, NULL);
        SetSegoeFont(hStatus, 9, FALSE);

        AppendLog(L"Dragon Builder GUI v1.0\r\n");
        AppendLog(L"1. Select your Python project folder\r\n");
        AppendLog(L"2. Enter the main entry file name\r\n");
        AppendLog(L"3. Click Build\r\n\r\n");
        break;
    }

    case WM_DRAWITEM: {
        if ((UINT)wParam == IDC_BUILD) {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            /* Rounded rect background */
            HBRUSH hbr;
            if (dis->itemState & ODS_SELECTED)
                hbr = CreateSolidBrush(RGB(0, 100, 180));
            else if (dis->itemState & ODS_HOTLIGHT)
                hbr = CreateSolidBrush(RGB(0, 130, 220));
            else
                hbr = CreateSolidBrush(RGB(ACCENT_R, ACCENT_G, ACCENT_B));

            HPEN hpen = CreatePen(PS_SOLID, 1, RGB(0, 90, 160));
            HPEN oldPen = SelectObject(hdc, hpen);
            HBRUSH oldBrush = SelectObject(hdc, hbr);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

            /* Text */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            HFONT oldFont = SelectObject(hdc, (HFONT)SendMessageW(hBuild, WM_GETFONT, 0, 0));
            DrawTextW(hdc, L"  Build", -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

            SelectObject(hdc, oldFont);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(hpen);
            DeleteObject(hbr);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(50, 50, 50));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case IDC_BROWSE: {
                wchar_t path[MAX_PATH] = {0};
                if (BrowseForFolder(hWnd, path, MAX_PATH)) {
                    SetWindowTextW(hDir, path);
                    /* Auto-detect main.py if present */
                    wchar_t test[MAX_PATH];
                    _snwprintf(test, MAX_PATH, L"%s\\main.py", path);
                    if (GetFileAttributesW(test) != INVALID_FILE_ATTRIBUTES)
                        SetWindowTextW(hEntry, L"main.py");
                }
                break;
            }
            case IDC_BUILD:
                OnBuild();
                break;
            }
        }
        break;

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        SetWindowPos(hDir, NULL, PAD, PAD+20, w - PAD*3 - 90, EDIT_H, SWP_NOZORDER);
        SetWindowPos(hBrowse, NULL, w - PAD - 90, PAD+19, 90, BTN_H+2, SWP_NOZORDER);
        SetWindowPos(hEntry, NULL, PAD, PAD+68, w - PAD*2, EDIT_H, SWP_NOZORDER);
        SetWindowPos(hBuild, NULL, PAD, PAD+108, 120, BTN_H+4, SWP_NOZORDER);
        SetWindowPos(hLog, NULL, PAD, PAD+152, w - PAD*2, h - PAD*2 - 180, SWP_NOZORDER);
        SetWindowPos(hStatus, NULL, 0, h - 24, w, 24, SWP_NOZORDER);
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 500;
        mmi->ptMinTrackSize.y = 360;
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

/* --------------------------------------------------------------- */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrev; (void)lpCmdLine;
    g_hInst = hInst;

    /* Init COM for modern file dialog */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* Create fonts */
    HDC hdcScreen = GetDC(NULL);
    int px10 = -MulDiv(10, GetDeviceCaps(hdcScreen, LOGPIXELSY), 72);
    int px9  = -MulDiv(9,  GetDeviceCaps(hdcScreen, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdcScreen);

    g_fontMain = CreateFontW(px10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    g_fontMono = CreateFontW(px9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

    /* Register window class */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"DragonBuilderGUI";
    wc.hbrBackground = CreateSolidBrush(RGB(245, 246, 247));
    RegisterClassExW(&wc);

    /* Create window */
    HWND hWnd = CreateWindowExW(
        WS_EX_OVERLAPPEDWINDOW | WS_EX_CLIENTEDGE,
        L"DragonBuilderGUI", APP_NAME,
        WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME) | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
        NULL, NULL, hInst, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    /* Message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_fontMain);
    DeleteObject(g_fontMono);
    CoUninitialize();
    return (int)msg.wParam;
}
