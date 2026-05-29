// ========================= PART 1 =========================
//   INCLUDES, DEFINES, VALIDATION, BASE64, FILE READER
// ==========================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <windows.h>   // Windows-only build

#define MAX_NAME 100
#define MAX_SOURCE_SIZE (1 << 20)   // 1 MB max input
#define MAX_PATH_LEN 1024
#define MAX_CMD 4096
#define SENTINEL "<<END>>"          // End of Python input marker

// ----------------------------------------------------------
// Validate module name (letters/digits/underscore, not empty)
// ----------------------------------------------------------
int is_valid_modname(const char *s) {
    if (!s || !s[0]) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return 0;

    for (size_t i = 1; s[i]; ++i) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_'))
            return 0;
    }
    return 1;
}

// ----------------------------------------------------------
// Clear stdin leftovers (prevents scanf/fgets issues)
// ----------------------------------------------------------
void clear_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

// ----------------------------------------------------------
// Base64 encoder (clean + fixed padding logic)
// ----------------------------------------------------------
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64_encode(const unsigned char* data, size_t in_len) {
    size_t out_len = 4 * ((in_len + 2) / 3);
    char* out = (char*)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    while (i < in_len) {
        uint8_t a = data[i++];
        uint8_t b = (i < in_len) ? data[i++] : 0;
        uint8_t c = (i < in_len) ? data[i++] : 0;

        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        
        if (i - 2 < in_len)
            out[j++] = b64_table[(triple >> 6) & 0x3F];
        else
            out[j++] = '=';
            
        if (i - 1 < in_len)
            out[j++] = b64_table[triple & 0x3F];
        else
            out[j++] = '=';
    }
    out[j] = '\0';
    return out;
}

// ----------------------------------------------------------
// Read file (safe, size-checked)
// ----------------------------------------------------------
unsigned char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f); return NULL;
    }

    long sz = ftell(f);
    if (sz < 0) {
        fclose(f); return NULL;
    }
    rewind(f);

    unsigned char* buf = (unsigned char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f); return NULL;
    }

    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (r != (size_t)sz) {
        free(buf);
        return NULL;
    }

    buf[sz] = 0;
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

// ----------------------------------------------------------
// Scan Python files for top-level import names
//   Walks projectDir recursively, reads .py files, extracts
//   'import X' and 'from X import ...' top-level modules.
//   Writes unique names into out_buf (comma-separated).
//   Returns number of modules found.
// ----------------------------------------------------------
static int is_stdlib_module(const char *name) {
    static const char *stdlibs[] = {
        "abc","argparse","array","ast","asyncio","atexit","base64","bdb",
        "binascii","builtins","bz2","calendar","cgi","cgitb","chunk",
        "cmath","cmd","code","codecs","codeop","collections","colorsys",
        "compileall","concurrent","configparser","contextlib","copy","copyreg",
        "cProfile","crypt","csv","ctypes","curses","dataclasses","datetime",
        "dbm","decimal","difflib","dis","distutils","doctest","email",
        "encodings","enum","errno","faulthandler","fcntl","filecmp","fileinput",
        "fnmatch","fractions","ftplib","functools","gc","getopt","getpass",
        "gettext","glob","graphlib","grp","gzip","hashlib","heapq","hmac",
        "html","http","idlelib","imaplib","imghdr","importlib","inspect","io",
        "ipaddress","itertools","json","keyword","lib2to3","linecache","locale",
        "logging","lzma","mailbox","mailcap","marshal","math","mimetypes",
        "mmap","modulefinder","multiprocessing","netrc","nis","nntplib",
        "numbers","operator","optparse","os","ossaudiodev","pathlib","pdb",
        "pickle","pickletools","pipes","pkgutil","platform","plistlib",
        "poplib","posix","posixpath","pprint","profile","pstats","pty",
        "pwd","py_compile","pyclbr","pydoc","queue","quopri","random","re",
        "readline","reprlib","resource","rlcompleter","runpy","sched","secrets",
        "select","selectors","shelve","shlex","shutil","signal","site",
        "smtpd","smtplib","sndhdr","socket","socketserver","spwd","sqlite3",
        "ssl","stat","statistics","string","stringprep","struct","subprocess",
        "sunau","symtable","sys","sysconfig","syslog","tabnanny","tarfile",
        "telnetlib","tempfile","termios","textwrap","threading","time",
        "timeit","tkinter","token","tokenize","trace","traceback","tracemalloc",
        "tty","turtle","turtledemo","types","typing","unicodedata","unittest",
        "urllib","uu","uuid","venv","warnings","wave","weakref","webbrowser",
        "winreg","winsound","wsgiref","xdrlib","xml","xmlrpc","zipapp",
        "zipfile","zlib",NULL
    };
    for (int i = 0; stdlibs[i]; i++)
        if (_stricmp(name, stdlibs[i]) == 0) return 1;
    return 0;
}

static int scan_python_imports(const char *projectDir, char *out_buf, size_t out_sz) {
    char search_path[MAX_PATH_LEN];
    snprintf(search_path, sizeof(search_path), "%s\\*.py", projectDir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search_path, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        // Try recursive search with FindFirstFile if needed, but for now
        // we use a simpler dir + parse approach via system() + temp file.
        // Actually, let's walk recursively using FindFirstFile properly.
    }
    if (h != INVALID_HANDLE_VALUE) FindClose(h);

    // Build a list of all .py files recursively using dir /s /b
    char tmp_list[MAX_PATH_LEN];
    snprintf(tmp_list, sizeof(tmp_list), "pyfiles_%lu.txt", GetTickCount());
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd),
        "dir /s /b \"%s\\*.py\" > \"%s\" 2>nul",
        projectDir, tmp_list);
    system(cmd);

    FILE *flist = fopen(tmp_list, "r");
    if (!flist) { remove(tmp_list); return 0; }

    char modules[256][64];
    int mod_count = 0;
    char line[MAX_PATH_LEN];

    while (fgets(line, sizeof(line), flist)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;

        FILE *fpy = fopen(line, "r");
        if (!fpy) continue;

        char pline[1024];
        while (fgets(pline, sizeof(pline), fpy)) {
            // strip leading whitespace
            char *p = pline;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#') continue;   // skip comments

            // match: import X, Y, Z
            if (strncmp(p, "import ", 7) == 0) {
                char buf[512];
                strncpy(buf, p + 7, sizeof(buf)-1);
                buf[sizeof(buf)-1] = '\0';
                char *tok = strtok(buf, ", \t\r\n");
                while (tok) {
                    // extract top-level name (before first dot)
                    char top[64] = {0};
                    size_t i = 0;
                    while (tok[i] && tok[i] != '.' && i < sizeof(top)-1) {
                        top[i] = tok[i]; i++;
                    }
                    top[i] = '\0';
                    if (top[0] && !is_stdlib_module(top)) {
                        // deduplicate
                        int dup = 0;
                        for (int j = 0; j < mod_count; j++)
                            if (_stricmp(modules[j], top) == 0) { dup = 1; break; }
                        if (!dup && mod_count < 256) {
                            strncpy(modules[mod_count], top, 63);
                            modules[mod_count][63] = '\0';
                            mod_count++;
                        }
                    }
                    tok = strtok(NULL, ", \t\r\n");
                }
            }
            // match: from X import ...
            else if (strncmp(p, "from ", 5) == 0) {
                char buf[512];
                strncpy(buf, p + 5, sizeof(buf)-1);
                buf[sizeof(buf)-1] = '\0';
                char *space = strchr(buf, ' ');
                if (space) {
                    *space = '\0';
                    // skip relative imports
                    if (buf[0] == '.') continue;
                    // extract top-level name
                    char top[64] = {0};
                    size_t i = 0;
                    while (buf[i] && buf[i] != '.' && i < sizeof(top)-1) {
                        top[i] = buf[i]; i++;
                    }
                    top[i] = '\0';
                    if (top[0] && !is_stdlib_module(top)) {
                        int dup = 0;
                        for (int j = 0; j < mod_count; j++)
                            if (_stricmp(modules[j], top) == 0) { dup = 1; break; }
                        if (!dup && mod_count < 256) {
                            strncpy(modules[mod_count], top, 63);
                            modules[mod_count][63] = '\0';
                            mod_count++;
                        }
                    }
                }
            }
        }
        fclose(fpy);
    }
    fclose(flist);
    remove(tmp_list);

    // Write comma-separated list into out_buf
    out_buf[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < mod_count; i++) {
        size_t need = strlen(modules[i]) + (i > 0 ? 2 : 0);
        if (pos + need + 1 >= out_sz) break;
        if (i > 0) {
            out_buf[pos++] = ','; out_buf[pos++] = ' ';
        }
        strcpy(out_buf + pos, modules[i]);
        pos += strlen(modules[i]);
    }
    out_buf[pos] = '\0';
    return mod_count;
}

// ----------------------------------------------------------
// Check if a command/executable is available on PATH
// ----------------------------------------------------------
int check_dependency(const char *cmd) {
    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "where %s >nul 2>&1", cmd);
    int rc = system(full_cmd);
    return rc == 0;
}

// ----------------------------------------------------------
// Check if a directory exists (WINDOWS NATIVE VERSION)
// ----------------------------------------------------------
int directory_exists(const char *path) {
    DWORD dwAttrib = GetFileAttributesA(path);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
            (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

// ====================== END OF PART 1 ======================
// ========================= PART 2 =========================
//     PYTHON PROJECT → RUN PYARMOR 6.8.1
// ==========================================================

int main(int argc, char **argv) {

    char projectName[MAX_NAME];
    char mainEntryName[MAX_NAME];
    char timestamp[32];

    time_t t = time(NULL);
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)t);

    printf("=== Python Hardened Builder (Windows-only) ===\n");
    printf("[*] Checking dependencies...\n");
    if (!check_dependency("python")) {
        fprintf(stderr, "[!] ERROR: Python not found. Install Python and add to PATH.\n");
        return 1;
    }
    if (!check_dependency("7z")) {
        fprintf(stderr, "[!] ERROR: 7z not found. Install 7-Zip and add to PATH.\n");
        return 1;
    }
    printf("[+] Dependencies OK\n\n");

    printf("Project directory name: ");
    if (scanf("%99s", projectName) != 1) {
        fprintf(stderr, "Error: could not read project directory name.\n");
        return 1;
    }
    clear_stdin();

    if (!is_valid_modname(projectName)) {
        fprintf(stderr, "Invalid project directory name format.\n");
        return 1;
    }

    printf("Main entry file (e.g., main.py): ");
    if (scanf("%99s", mainEntryName) != 1) {
        fprintf(stderr, "Error: could not read main entry file name.\n");
        return 1;
    }
    clear_stdin();

    if (!directory_exists(projectName)) {
        fprintf(stderr, "[!] ERROR: Project directory '%s' not found.\n", projectName);
        return 1;
    }

    char main_entry_path[1024];
    snprintf(main_entry_path, sizeof(main_entry_path), "%s\\%s", projectName, mainEntryName);
    FILE *fmain_check = fopen(main_entry_path, "r");
    if (!fmain_check) {
        fprintf(stderr, "[!] ERROR: Main entry file '%s' not found in directory '%s'.\n", mainEntryName, projectName);
        return 1;
    }
    fclose(fmain_check);
    
    printf("[+] Found project '%s' with entry file '%s'\n", projectName, mainEntryName);

    // --- Scan project for third-party imports ---
    char detected_modules[4096];
    int mod_count = scan_python_imports(projectName, detected_modules, sizeof(detected_modules));
    if (mod_count > 0) {
        printf("[+] Auto-detected third-party imports (%d): %s\n", mod_count, detected_modules);
    } else {
        printf("[*] No third-party imports detected (or all are stdlib).\n");
    }

    printf("[*] Ensuring PyArmor 6.8.1 is installed...\n");
    system("python -m pip install --upgrade pip >nul 2>&1");
    system("python -m pip install pyarmor==6.8.1 >nul 2>&1");

    char out_dir[256];
    snprintf(out_dir, sizeof(out_dir), "pyarmor_%s", timestamp);

    char cmd[1024];
    snprintf(
        cmd, sizeof(cmd),
        "pyarmor obfuscate --output \"%s\" "
        "--advanced 2 --restrict 1 \"%s\"",
        out_dir, projectName
    );

    printf("[*] Running PyArmor on project directory...\n");
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "[!] PyArmor failed (rc=%d)\n", rc);
        return 1;
    }

    char obf_path[512];
    snprintf(obf_path, sizeof(obf_path),
             "%s\\%s", out_dir, mainEntryName);

    FILE *ftest = fopen(obf_path, "rb");
    if (!ftest) {
        fprintf(stderr,
                "[!] ERROR: PyArmor did not output %s\n"
                "    Something failed in obfuscation.\n",
                obf_path
        );
        return 1;
    }
    fclose(ftest);

    printf("[+] PyArmor output confirmed: %s\n", obf_path);

// ====================== END OF PART 2 ======================
// ========================= PART 3 =========================
//    ZIP PYARMOR OUTPUT → BASE64 → WRITE .PYX LOADER
// ==========================================================

    char zipname[512];
    snprintf(zipname, sizeof(zipname),
             "pyarmor_payload_%s.zip", timestamp);

    snprintf(
        cmd, sizeof(cmd),
        "7z a -tzip \"%s\" \"%s\\*\" -mx=9 >nul 2>&1",
        zipname, out_dir
    );

    printf("[*] Creating ZIP of PyArmor output...\n");
    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr,
            "[!] ERROR: 7z failed (is 7-Zip installed and on PATH?)\n");
        return 1;
    }
    FILE *fzip = fopen(zipname, "rb");
    if (!fzip) {
        fprintf(stderr, "[!] ERROR: ZIP file %s was not created.\n", zipname);
        return 1;
    }
    fclose(fzip);

    printf("[+] Created ZIP archive: %s\n", zipname);

    size_t zip_size = 0;
    unsigned char *zipbuf = read_file(zipname, &zip_size);

    if (!zipbuf || zip_size == 0) {
        fprintf(stderr,
            "[!] ERROR: Failed to read ZIP archive into memory.\n");
        return 1;
    }

    printf("[+] ZIP read (%zu bytes)\n", zip_size);

    char *zip_b64 = base64_encode(zipbuf, zip_size);
    free(zipbuf);

    if (!zip_b64) {
        fprintf(stderr,
            "[!] ERROR: Base64 encoding failed.\n");
        return 1;
    }

    printf("[+] Base64 encoded ZIP (%zu chars)\n", strlen(zip_b64));

// ====================== END OF PART 3 ======================
// ========================= PART 4 =========================
//    CREATE LAUNCHER + BUILD WITH NUITKA
// =========================================================

    char launcher_py[512];
    snprintf(launcher_py, sizeof(launcher_py),
             "launcher_%s.py", timestamp);

    FILE *flauncher = fopen(launcher_py, "w");
    if (!flauncher) {
        fprintf(stderr,
            "[!] ERROR: Could not write launcher %s\n", launcher_py);
        free(zip_b64);
        return 1;
    }

    fprintf(flauncher,
        "import os, sys, base64, zipfile, tempfile, runpy, ctypes, io, shutil, time\n"
    );
    // Write auto-detected third-party imports
    if (mod_count > 0) {
        char mod_buf[4096];
        strncpy(mod_buf, detected_modules, sizeof(mod_buf)-1);
        mod_buf[sizeof(mod_buf)-1] = '\0';
        char *tok = strtok(mod_buf, ", ");
        while (tok) {
            fprintf(flauncher,
                "try:\n"
                "    import %s\n"
                "except Exception:\n"
                "    pass\n",
                tok
            );
            tok = strtok(NULL, ", ");
        }
    }
    fprintf(flauncher, "pyarmor_zip_b64 = (\n");

    const size_t CHUNK = 80;
    size_t zb_len = strlen(zip_b64);
    size_t pos = 0;

    while (pos < zb_len) {
        size_t remain = zb_len - pos;
        size_t take = remain > CHUNK ? CHUNK : remain;

        fprintf(flauncher, "    \"");
        for (size_t i = 0; i < take; ++i) {
            char c = zip_b64[pos + i];
            if (c == '\\') fprintf(flauncher, "\\\\");
            else if (c == '\"') fprintf(flauncher, "\\\"");
            else fputc(c, flauncher);
        }
        fprintf(flauncher, "\"\n");

        pos += take;
    }
    fprintf(flauncher, ")\n\n");

    // *** THIS IS THE FINAL, CORRECTED LAUNCHER SCRIPT ***
    fprintf(flauncher,
        "# Enhanced Anti-Debug & Sandbox Detection\n"
        "def anti_debug():\n"
        "    try:\n"
        "        import os, sys, time, ctypes\n"
        "    except Exception:\n"
        "        return\n"
        "    # Python debugger detection\n"
        "    try:\n"
        "        if getattr(sys, 'gettrace', None) and sys.gettrace():\n"
        "            raise SystemExit('Debugger detected')\n"
        "    except Exception:\n"
        "        pass\n"
        "    # Native Windows debugger detection\n"
        "    try:\n"
        "        if os.name == 'nt' and ctypes.windll.kernel32.IsDebuggerPresent():\n"
        "            raise SystemExit('Debugger detected')\n"
        "    except Exception:\n"
        "        pass\n"
        "    # Suspicious environment variables\n"
        "    suspicious_env = ['PYCHARM_HOSTED','VSCODE_PID','TERM_PROGRAM','PYDEV_CONSOLE_ENCODING']\n"
        "    for k in suspicious_env:\n"
        "        if os.environ.get(k):\n"
        "            raise SystemExit('Analysis environment detected')\n"
        "    # Sandbox DLL detection\n"
        "    sandbox_dlls = ['sbiedll.dll','api_log.dll','dir_watch.dll','pstorec.dll']\n"
        "    for dll in sandbox_dlls:\n"
        "        try:\n"
        "            if ctypes.windll.kernel32.GetModuleHandleW(dll):\n"
        "                raise SystemExit('Sandbox detected')\n"
        "        except Exception:\n"
        "            pass\n"
        "    # Timing anomaly detection\n"
        "    start_time = time.time()\n"
        "    [x*x for x in range(100000)]\n"
        "    if time.time() - start_time > 0.5:\n"
        "            raise SystemExit('Timing anomaly detected')\n"
        "    # Temp-path heuristic\n"
        "    try:\n"
        "        if 'temp' in os.path.abspath(__file__).lower():\n"
        "            time.sleep(1)\n"
        "    except Exception:\n"
        "        pass\n"
        "    # Window-title analysis tool detection\n"
        "    try:\n"
        "        import ctypes.wintypes\n"
        "        detected = {'hit': False}\n"
        "        EnumWindows = ctypes.windll.user32.EnumWindows\n"
        "        EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.wintypes.BOOL, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)\n"
        "        GetWindowText = ctypes.windll.user32.GetWindowTextW\n"
        "        GetWindowTextLength = ctypes.windll.user32.GetWindowTextLengthW\n"
        "        blacklisted_titles = ['ollydbg','x64dbg','ida','wireshark','process monitor','process hacker','cheat engine','fiddler','dependency walker']\n"
        "        def foreach_window(hwnd, lParam):\n"
        "            length = GetWindowTextLength(hwnd)\n"
        "            if length > 0:\n"
        "                buff = ctypes.create_unicode_buffer(length + 1)\n"
        "                GetWindowText(hwnd, buff, length + 1)\n"
        "                title = buff.value.lower()\n"
        "                for bad in blacklisted_titles:\n"
        "                    if bad in title:\n"
        "                        detected['hit'] = True\n"
        "                        return False\n"
        "            return True\n"
        "        EnumWindows(EnumWindowsProc(foreach_window), 0)\n"
        "        if detected['hit']:\n"
        "            raise SystemExit('Analysis tool detected')\n"
        "    except Exception:\n"
        "        pass\n"
        "anti_debug()\n\n"
        "# --- Payload Extraction and Execution ---\n"
        "from io import BytesIO\n"
        "b = base64.b64decode(pyarmor_zip_b64)\n"
        "if not zipfile.is_zipfile(BytesIO(b)):\n"
        "    raise SystemExit('Invalid payload')\n"
        "tmp_dir = tempfile.mkdtemp(prefix='pyarmor_run_')\n"
        "with zipfile.ZipFile(BytesIO(b), 'r') as z: z.extractall(tmp_dir)\n\n"
        "# Find the entry script AND the payload root directory\n"
        "entry_script_path = None\n"
        "payload_root = None\n"
        "for root, dirs, files in os.walk(tmp_dir):\n"
        "    if '%s' in files:\n"
        "        entry_script_path = os.path.join(root, '%s')\n"
        "        payload_root = root\n"
        "        break\n\n"
        "if not entry_script_path or not payload_root:\n"
        "    raise SystemExit('Could not find PyArmor payload or entry script.')\n\n"
        "# *** THE KEY FIX IS HERE ***\n"
        "# Add the payload root to sys.path. This makes pytransform.pyd visible.\n"
        "if payload_root not in sys.path:\n"
        "    sys.path.insert(0, payload_root)\n"
        "    os.environ['PYARMOR_RUNTIME_PATH'] = payload_root\n\n"
        "# Execute the main script using run_path\n"
        "try:\n"
        "    runpy.run_path(entry_script_path, run_name='__main__')\n"
        "finally:\n"
        "    try:\n"
        "        shutil.rmtree(tmp_dir)\n"
        "    except Exception:\n"
        "        pass\n"
        "    del b\n"
        "    pyarmor_zip_b64 = None\n",
        mainEntryName, mainEntryName
    );

    fclose(flauncher);
    free(zip_b64);

    printf("[+] Created self-contained launcher script: %s\n", launcher_py);

// ====================== END OF PART 4 ======================
// ========================= PART 5 =========================
//   BUILD FINAL EXE (NUITKA) + OPTIONAL UPX (Windows)
// ==========================================================

    char exe_name[512];
    snprintf(exe_name, sizeof(exe_name), "%s.exe", projectName);

    char dist_dir[256];
    snprintf(dist_dir, sizeof(dist_dir), "dist_%s", timestamp);

    // Build dynamic --include-package flags from detected modules
    char include_flags[4096] = {0};
    if (mod_count > 0) {
        char mod_buf[4096];
        strncpy(mod_buf, detected_modules, sizeof(mod_buf)-1);
        mod_buf[sizeof(mod_buf)-1] = '\0';
        size_t fpos = 0;
        char *tok = strtok(mod_buf, ", ");
        while (tok) {
            size_t need = snprintf(NULL, 0, " --include-package=%s", tok);
            if (fpos + need + 1 < sizeof(include_flags)) {
                fpos += snprintf(include_flags + fpos, sizeof(include_flags) - fpos,
                                 " --include-package=%s", tok);
            }
            tok = strtok(NULL, ", ");
        }
    }

    const char* nuitka_bases[] = {
        "python -m nuitka --onefile --assume-yes-for-downloads%s --output-dir=%s --output-filename=%s %s",
        "py -3.9 -m nuitka --onefile --assume-yes-for-downloads%s --output-dir=%s --output-filename=%s %s",
        "py -3 -m nuitka --onefile --assume-yes-for-downloads%s --output-dir=%s --output-filename=%s %s",
        NULL
    };

    printf("[*] Running Nuitka...\n");
    int nuitka_success = -1;
    for (int i = 0; nuitka_bases[i] != NULL; i++) {
        snprintf(cmd, sizeof(cmd), nuitka_bases[i],
                 include_flags, dist_dir, exe_name, launcher_py);
        printf("[*] Trying: %s\n", cmd);
        int rc_n = system(cmd);
        if (rc_n == 0) {
            nuitka_success = i;
            printf("[+] Nuitka command succeeded\n");
            break;
        } else {
            printf("[-] Nuitka command failed (rc=%d)\n", rc_n);
        }
    }

    if (nuitka_success == -1) {
        fprintf(stderr,
            "[!] ERROR: All Nuitka commands failed.\n"
            "    Ensure Nuitka and a C compiler are installed.\n"
        );
        return 1;
    }

    char exe_path[1024];
    snprintf(exe_path, sizeof(exe_path), "%s\\%s", dist_dir, exe_name);
    FILE *fexe = fopen(exe_path, "rb");
    if (!fexe) {
        fprintf(stderr, "[!] ERROR: EXE not found: %s\n", exe_path);
        system("dir dist_*");
        return 1;
    }
    fclose(fexe);
    printf("[+] Nuitka build verified: %s\n", exe_path);

#ifdef _WIN32
    if (check_dependency("upx")) {
        snprintf(
            cmd, sizeof(cmd),
            "upx --lzma --best --ultra-brute \"%s\"", exe_path
        );
        printf("[*] Running UPX...\n");
        if(system(cmd) != 0) {
            fprintf(stderr, "[!] UPX failed on: %s\n", exe_path);
        } else {
            printf("[+] UPX compression successful.\n");
        }
    } else {
        printf("[*] UPX not found, skipping compression.\n");
    }
#endif

    printf("\n=== All done ===\n");
    printf("Project directory: %s\n", projectName);
    printf("Main entry file: %s\n", mainEntryName);
    printf("Final EXE: %s\n", exe_path);

    return 0;
}