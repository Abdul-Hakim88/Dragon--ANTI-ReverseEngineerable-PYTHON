# Dragon — Python Hardened Builder v1.5

---

## Overview

Dragon is a Windows tool written in C that takes a Python project directory and produces a standalone, hardened `.exe` through a four-stage pipeline:

1. **PyArmor obfuscation** — bytecode-level obfuscation with license enforcement
2. **ZIP + Base64 embedding** — the obfuscated payload is archived and embedded as a Base64 blob
3. **Self-extracting launcher** — a generated Python script decodes, validates, and runs the payload in a temp directory
4. **Nuitka compilation** — the launcher is compiled to a native Windows executable
5. **UPX packing** *(optional)* — final LZMA compression pass

The result is a single `.exe` with no visible Python source, no importable `.py` files, and active runtime anti-analysis checks.

---

## Pipeline

```
Python project directory
        │
        ▼
┌───────────────────────┐
│  1. Scan .py files    │  ← Auto-detects third-party imports (non-stdlib)
│     for imports       │
└───────────────────────┘
        │
        ▼
┌───────────────────────┐
│  2. PyArmor 6.8.1     │  ← --advanced 2 --restrict 1
│     obfuscation       │     (advanced mode, restricted import)
└───────────────────────┘
        │
        ▼
┌───────────────────────┐
│  3. 7z ZIP + Base64   │  ← -mx=9 max compression
│     encode payload    │
└───────────────────────┘
        │
        ▼
┌───────────────────────┐
│  4. Generate launcher │  ← Embeds Base64 blob + anti-debug checks
│     launcher_<ts>.py  │     + runtime extraction + runpy execution
└───────────────────────┘
        │
        ▼
┌───────────────────────┐
│  5. Nuitka --onefile  │  ← Compiles to native PE, no IL/bytecode exposed
│     + auto --include  │     Dynamic --include-package for detected imports
└───────────────────────┘
        │
        ▼
┌───────────────────────┐
│  6. UPX LZMA pack     │  ← Optional, --ultra-brute
│     (if on PATH)      │
└───────────────────────┘
        │
        ▼
  <projectname>.exe  (self-contained, hardened)
```

---

## Anti-Analysis Techniques (Runtime Launcher)

The generated launcher embeds the following checks before payload extraction:

| Technique | Method |
|---|---|
| Python trace hook detection | `sys.gettrace()` — detects `pdb`, `pydevd`, coverage hooks |
| Native debugger detection | `kernel32.IsDebuggerPresent()` via ctypes |
| Suspicious environment variables | Checks for `PYCHARM_HOSTED`, `VSCODE_PID`, `PYDEV_CONSOLE_ENCODING`, etc. |
| Sandbox DLL detection | `GetModuleHandleW` scan for Sandboxie, API Monitor DLLs |
| Timing anomaly detection | Measures CPU-bound loop; flags slowdown > 0.5s (emulator indicator) |
| Temp-path heuristic | Delays execution if `__file__` path contains `temp` |
| Window title scan | `EnumWindows` + title matching against debugger/analysis tool names |

If any check triggers, the process exits cleanly with no error output.

---

## Import Auto-Detection

Dragon walks the project directory recursively, parses all `.py` files, and extracts third-party `import` / `from X import` statements — filtering out the entire Python stdlib (150+ modules). Detected packages are:

- Injected as `try/except import` stubs in the launcher
- Passed as `--include-package=<name>` flags to Nuitka automatically

This ensures the compiled exe bundles all required dependencies without manual configuration.

---

## Build Requirements

| Requirement | Notes |
|---|---|
| Windows 10/11 x64 | ctypes P/Invoke calls are Windows-only |
| Python 3.x | Must be on `PATH` as `python` |
| PyArmor 6.8.1 | Auto-installed via pip if missing |
| 7-Zip (`7z`) | Must be on `PATH` |
| Nuitka | `python -m pip install nuitka`; also tries `py -3.9` and `py -3` |
| C compiler | Required by Nuitka (MSVC or MinGW) |
| UPX *(optional)* | Enables final LZMA compression pass |

---

## Usage

```
Dragon_1_5.exe
```

Prompts:
1. **Project directory name** — folder containing your `.py` files
2. **Main entry file** — e.g. `main.py`

Outputs:
- `pyarmor_<ts>/` — obfuscated Python files
- `pyarmor_payload_<ts>.zip` — compressed payload archive
- `launcher_<ts>.py` — generated self-extracting launcher
- `dist_<ts>/<projectname>.exe` — final hardened executable

---

## Version History

### v1.0
- Basic PyArmor obfuscation + Nuitka compilation pipeline
- Manual import specification

### v1.5 *(current)*
- Automatic third-party import scanning across full project tree
- Dynamic `--include-package` flag generation for Nuitka
- Enhanced anti-debug launcher (timing, env vars, window scan, sandbox DLLs)
- Improved PyArmor path resolution (`pytransform.pyd` fix via `sys.path` injection)
- Temp directory cleanup in `finally` block
- Base64 payload integrity check before extraction


---

## References

- [PyArmor 6 Documentation](https://pyarmor.readthedocs.io/en/v6.8.1/)
- [Nuitka Documentation](https://nuitka.net/doc/user-manual.html)
- [Python Anti-Debugging Techniques](https://blog.quarkslab.com/protecting-a-python-codebase.html) — Quarkslab
- *Practical Malware Analysis* — Sikorski & Honig, Chapter 17 (Sandbox evasion)

---

