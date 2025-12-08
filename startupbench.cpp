// startupbench.cpp --- GUIプログラムの起動時間を測定する
#include <cstdlib>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <locale>
#include <windows.h>
#include <shlwapi.h>

// バージョン情報を出力
void version() {
    wprintf(L"startupbench version 1.0 by katahiromz\n");
}

// 使い方を出力
void usage() {
    wprintf(
        L"startupbench --- GUIプログラムの起動時間を計測する\n"
        L"\n"
        L"使い方 1: startupbench --help\n"
        L"使い方 2: startupbench --version\n"
        L"使い方 3: startupbench CLASS_NAME program.exe ...\n"
        L"  CLASS_NAME       対象のウィンドウクラス名\n"
        L"  program.exe ...  測定対象のGUIプログラムとコマンドライン引数\n"
        L"\n"
        L"例 1: startupbench Notepad notepad.exe             (メモ帳の起動時間を測定)\n"
        L"例 2: startupbench Notepad notepad.exe \"file.txt\"  (ファイルを指定してメモ帳の起動時間を測定)\n"
        L"\n"
        L"NOTE: メモ帳(Notepad)は大きいファイル（32MB+ or 1GB+）を開けないので注意\n"
    );
}

DWORD g_pid = 0; // 測定対象のプロセスID
HANDLE g_hProcess = nullptr; // 測定対象のプロセスハンドル

// Ctrl+Cが押されたら呼び出されるコールバック関数
BOOL CALLBACK HandlerRoutine(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        if (g_pid && g_hProcess) {
            wprintf(L"startupbench: 子プロセス終了中... (pid: %lu)\n", g_pid);
            TerminateProcess(g_hProcess, -1);
            wprintf(L"startupbench: 子プロセス終了 (pid: %lu)\n", g_pid);
        }
    }
    return FALSE;
}

// ストップウォッチ
class StopWatch {
public:
    LARGE_INTEGER m_freq; // 周波数
    LARGE_INTEGER m_start, m_stop; // 開始時間と終了時間
    BOOL m_qp_supported; // 計測用のQueryPerformanceCounterがサポートされているか？
    StopWatch() : m_qp_supported() {
        m_qp_supported = QueryPerformanceFrequency(&m_freq);
        if (!m_qp_supported) {
            wprintf(L"startupbench: QueryPerformanceFrequency エラー: %lu\n", GetLastError());
        }
    }
    bool start() {
        if (m_qp_supported) {
            if (QueryPerformanceCounter(&m_start))
                return true;
            wprintf(L"startupbench: QueryPerformanceCounter エラー: %lu\n", GetLastError());
            m_qp_supported = false;
        }
        m_start.QuadPart = (LONGLONG)GetTickCount64();
        return true;
    }
    double stop() {
        if (m_qp_supported) {
            if (!QueryPerformanceCounter(&m_stop)) {
                wprintf(L"startupbench: QueryPerformanceCounter エラー: %lu\n", GetLastError());
                return -1.0;
            }
        } else {
            m_stop.QuadPart = (LONGLONG)GetTickCount64();
        }
        LONGLONG delta = m_stop.QuadPart - m_start.QuadPart;
        double milliseconds = m_qp_supported ? ((double)delta * 1000.0 / (double)m_freq.QuadPart) : delta;
        return milliseconds;
    }
};

// ウィンドウを探すための構造体
struct FIND_WINDOW {
    LPCWSTR class_name; // クラス名
    DWORD pid; // プロセスID
    HWND found; // 見つかったウィンドウ
};

// ウィンドウを探すコールバック関数
BOOL CALLBACK find_window_proc(HWND hwnd, LPARAM lParam) {
    FIND_WINDOW* data = reinterpret_cast<FIND_WINDOW*>(lParam);

    // ウィンドウのプロセスIDを取得
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != data->pid)
        return TRUE; // 継続

    // ウィンドウクラス名を取得
    WCHAR cls_name[128];
    if (!GetClassNameW(hwnd, cls_name, _countof(cls_name)))
        return TRUE; // 継続

    if (lstrcmpiW(cls_name, data->class_name) != 0)
        return TRUE; // 継続

    data->found = hwnd; // 見つかった
    return FALSE; // 中断
}
 
// ウィンドウを閉じる
BOOL close_window(LPCWSTR class_name, DWORD pid) {
    // ウィンドウを探す
    FIND_WINDOW find_window_data = { class_name, pid, nullptr };
    EnumWindows(find_window_proc, (LPARAM)&find_window_data);
    HWND hwnd = find_window_data.found;
    if (!hwnd)
        return TRUE; // ウィンドウが見つからなかった

    // ウィンドウを閉じる
    DWORD_PTR result;
    if (!SendMessageTimeoutW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0, SMTO_ABORTIFHUNG, 3000, &result) &&
        !PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0))
    {
        DestroyWindow(hwnd);
    }
    for (int i = 0; i < 10 && IsWindow(hwnd); ++i)
        Sleep(100);

    if (IsWindow(hwnd)) {
        wprintf(L"startupbench: %ls ウィンドウが閉じられません。", class_name);
        return FALSE;
    }

    Sleep(800); // アプリ終了後のファイルのロック解除を少し待つ
    return TRUE;
}

// プログラムのクラス
class startupbench {
public:
    std::wstring m_class_name;
    std::wstring m_program;
    std::wstring m_parameters;

    // コマンドラインを解析する
    int parse_cmd_line(int argc, wchar_t **wargv) {
        // 初期化
        m_class_name.clear();
        m_program.clear();
        m_parameters.clear();

        // 引数がなければ使い方を表示する
        if (argc <= 1) {
            wprintf(L"startupbench: 引数がありません\n");
            usage();
            return -1;
        }

        // 引数が1個？
        if (argc == 2) {
            wchar_t *arg = wargv[1];
            if (_wcsicmp(arg, L"--help") == 0 || _wcsicmp(arg, L"/?") == 0) {
                usage();
                return +1;
            }
            if (_wcsicmp(arg, L"--version") == 0 || _wcsicmp(arg, L"-v") == 0) {
                version();
                return +1;
            }
            usage();
            return +1;
        }

        // 引数が3個以上の場合
        m_class_name = wargv[1]; // クラス名
        m_program = wargv[2]; // プログラム名

        // 引数群文字列を構築する
        for (int iarg = 3; iarg < argc; ++iarg) {
            if (iarg > 4)
                m_parameters += L' ';

            const wchar_t *arg = wargv[iarg];
            if (wcschr(arg, L' ') || wcschr(arg, L'\t')) {
                m_parameters += L'"';
                m_parameters += arg;
                m_parameters += L'"';
            } else {
                m_parameters += arg;
            }
        }

        return 0;
    }

    // プログラムの本体
    int run() {
        g_pid = 0;
        g_hProcess = nullptr;
        return measure_startup_time() ? 0 : 1;
    }

    // 測定する
    bool measure_startup_time() {
        // 時間の測定開始
        StopWatch stop_watch;
        if (!stop_watch.start())
            return false;

        // m_program を起動する
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = m_program.c_str();
        sei.lpParameters = m_parameters.empty() ? nullptr : m_parameters.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            wprintf(L"startupbench: %ls が起動できません(エラー: %lu)。\n", m_program.c_str(), GetLastError());
            return false;
        }
        if (!sei.hProcess) {
            wprintf(L"startupbench: sei.hProcessが nullptr でした。\n");
            return false;
        }
        DWORD pid = GetProcessId(sei.hProcess);

        g_hProcess = sei.hProcess;
        g_pid = pid;

        // Ctrl+Cが押されたら子プロセスを強制終了する
        SetConsoleCtrlHandler(HandlerRoutine, TRUE);

        // 起動するまで待つ
        DWORD wait = WaitForInputIdle(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
        if (wait == WAIT_FAILED) {
            DWORD error = GetLastError();
            if (error == ERROR_NOT_GUI_PROCESS) {
                wprintf(L"startupbench: %ls: GUIプログラムではありません\n", m_program.c_str());
            } else {
                wprintf(L"startupbench: %ls: WaitForInputIdle 失敗 (エラー: %lu)\n", m_program.c_str(), error);
            }
            return false;
        }

        // 時間の測定終了
        double milliseconds = stop_watch.stop();

        // 結果を表示する
        if (milliseconds < 0)
            wprintf(L"startupbench: %ls: 失敗\n", m_class_name.c_str());
        else
            wprintf(L"startupbench: %ls: %.2f [ミリ秒]\n", m_class_name.c_str(), milliseconds);

        // ウィンドウを閉じる
        close_window(m_class_name.c_str(), pid);

        g_hProcess = nullptr;
        g_pid = 0;

        return true; // 成功
    }
};

extern "C"
int wmain(int argc, wchar_t **wargv) {
    // wprintfの出力を正しく行うためのおまじない
    _setmode(_fileno(stdout), _O_U8TEXT);
    SetConsoleOutputCP(CP_UTF8);
    std::setlocale(LC_ALL, "");

    int ret = -1;
    {
        startupbench app_main;
        ret = app_main.parse_cmd_line(argc, wargv);
        if (ret == -1)
            return 1;
        if (ret == +1)
            return 0;
        ret = app_main.run();
    }

    // TODO: 必要ならここでメモリリークがないか調べる

    return ret;
}

// wmainをサポートしないコンパイラ用
int main(void) {
    int argc;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int ret = wmain(argc, wargv);
    LocalFree(wargv);
    return ret;
}
