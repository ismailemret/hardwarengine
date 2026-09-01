---
name: Bug report
about: Create a report to help us improve
title: "[BUG]"
labels: bug
assignees: ismailemret
type: Bug

---

---
name: Bug Report
about: Report a hardware telemetry, MSR read, driver, or performance counter issue
title: '[BUG] '
labels: bug
assignees: ''
---

**Describe the bug**
A clear and concise description of what the bug is (e.g., incorrect CPU frequency, failed MSR read, crash on startup, ring-0 driver initialization failure).

**To Reproduce**
Steps to reproduce the behavior:
1. Run `./build.bat` (or your specific build command)
2. Execute the binary with `hardwarengine.exe` (specify if run as Admin)
3. Trigger telemetry collection
4. See error or unexpected output

**Expected behavior**
A clear and concise description of what you expected to happen (e.g., correct core frequency readings, valid temperature metrics, clean driver handle cleanup).

**Hardware & System Environment (please complete the following information):**
- **OS & Build:** Windows 10 / 11 (e.g., Windows 11 23H2 / Build 22631.xxxx)
- **CPU Model:** [e.g., Intel Core i5-14500HX, AMD Ryzen 7 7800X3D]
- **GPU Model:** [e.g., NVIDIA GeForce RTX 4050, Integrated Graphics]
- **RAM Configuration:** [e.g., 16GB DDR5 4800MHz]
- **Privileges:** [Run as Administrator / Standard User]
- **Compiler / Toolchain:** [e.g., MSVC v143, MinGW-w64 GCC 13.2, Clang-CL]

**Logs & Console Output**
If applicable, paste the terminal output, error codes (Win32 `GetLastError()` or NTSTATUS codes), or crash logs here:
```text
[Paste terminal/debug logs here]
