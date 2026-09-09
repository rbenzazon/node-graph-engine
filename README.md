# node-graph-engine

C++20 **hybrid node-graph engine** for industrial dataflow: you author a structural graph (nodes, pins, wires, nesting), the engine **flattens** it, then runs a **transient Taskflow** schedule.

This repo is a **standalone** library + demos. It is not tied to any UI frontend.

| | |
|---|---|
| Language | C++20 |
| Build | CMake 3.20+ / Ninja |
| Scheduler dep | [Taskflow](https://github.com/taskflow/taskflow) (FetchContent) |
| Primary toolchain (Windows) | MSVC v143 + Windows 10 SDK |

Design details and the full implementation plan live in **[docs/initial-specs.md](docs/initial-specs.md)**.

---

## What this engine does

```text
Authoring graph          Compile                 Runtime
(nodes, pins, wires) --> flatten/splice ------> flat nodes
GraphNode macros           (no nesting           transient
Boundary pins               left)                Taskflow DAG
                                                 tf::Executor
```

**Locked ideas (short):**

1. **Everything is a pin** — no separate parameter system; unwired pins use literals.
2. **Custom graph is source of truth** — Taskflow is only a scheduler.
3. **Nesting is compile-time** — `GraphNode` instances are cloned and spliced away before run.
4. **One `compute(Slice)`** — nodes stay thread-agnostic; the engine may split buffer work.
5. **Dirty / emit control** — temporal gates and event logic are normal nodes + flags.

Current code status: **core engine implemented** (value/pin/node/graph/factory, validate + flatten with `GraphNode` splice, transient Taskflow scheduler with `ParallelHint`, temporal nodes, trigger queue, pipeline demo + expanded smoke tests). Optional later items (JSON, SHM, WaitSequence sugar, Shared lifetime) remain deferred — see [docs/initial-specs.md](docs/initial-specs.md).

---

## Repository layout

```text
node-graph-engine/
  README.md                 ← you are here
  BUILD.md                  ← short build cheat-sheet
  docs/
    initial-specs.md        ← full architecture + roadmap
  CMakeLists.txt
  CMakePresets.json
  cmake/Dependencies.cmake  ← FetchContent Taskflow
  include/node_engine/      ← public headers
  src/                      ← library sources
  nodes/                    ← node catalog (placeholders + examples)
  demos/                    ← runnable demos
  tests/                    ← ctest targets
  scripts/
    dev_env.ps1             ← load MSVC + Windows SDK into PowerShell
    build.ps1               ← configure + build + test
  .vscode/                  ← tasks/settings aimed at MSVC
```

---

## Prerequisites (Windows 10)

You need **all three**: compiler, SDK, and build tools.

### 1. CMake and Ninja

```powershell
winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
```

Open a **new** terminal afterward so `PATH` updates.

Check:

```powershell
cmake --version    # ≥ 3.20
ninja --version
```

### 2. Visual Studio 2022 Build Tools (MSVC) + Windows 10 SDK

**This is the primary toolchain.** Do not rely on Clang alone on Windows: LLVM’s `clang++` still needs the Windows SDK import libs (`kernel32.lib`, …).

#### Install / modify (GUI — recommended)

1. Install **Visual Studio Installer** if needed, then **Build Tools 2022**.
2. Open Installer → **Build Tools 2022** → **Modify**.
3. Enable workload **Desktop development with C++** (or equivalent VC tools).
4. Individual components — minimum:
   - **MSVC v143** (VS 2022 C++ x64/x86 build tools)
   - **Windows 10 SDK** (e.g. 10.0.19041 or newer listed)  
     On Windows 10, prefer a **Windows 10 SDK**, not only a Windows 11 SDK.
5. Optional (not required for this repo): CMake tools inside VS, vcpkg, testing tools.

#### CLI modify (must be elevated)

`--passive` / `--quiet` **must** run from an **Administrator** PowerShell.  
Do **not** pass `--wait` to this installer version (unknown option → exit 87).

```powershell
# Run as Administrator
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe" modify `
  --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" `
  --add Microsoft.VisualStudio.Workload.VCTools `
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  --add Microsoft.VisualStudio.Component.Windows10SDK.19041 `
  --includeRecommended `
  --passive
```

Use the Windows 10 SDK component ID that matches what the Installer UI shows if `19041` is not listed.

> **Note:** `winget install ...BuildTools --override "--add ...SDK..."` does **not** reliably add components to an existing install. Prefer **Modify** (GUI or `setup.exe modify`).

### 3. Verify the toolchain

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set WindowsSdkDir && set WindowsSDKVersion && where cl && where rc'
```

You want:

| Check | Good result |
|---|---|
| `WindowsSdkDir` | `...\Windows Kits\10\` |
| `WindowsSDKVersion` | e.g. `10.0.19041.0\` |
| `where cl` | MSVC `cl.exe` |
| `where rc` | Windows Kits `rc.exe` |

Also:

```powershell
Test-Path "C:\Program Files (x86)\Windows Kits\10\Lib"   # True
```

### Optional: LLVM Clang

```powershell
winget install --id LLVM.LLVM -e
```

Useful for tooling, **not** the default project compiler. If CMake picks Clang by mistake you get missing `kernel32.lib` until the SDK is installed *and* you force MSVC (below).

### Optional: Git

Needed for Taskflow `FetchContent` clone (and normal repo work).

```powershell
git --version
```

---

## Get started (build & run)

### Easiest path

From the repo root:

```powershell
cd path\to\node-graph-engine
.\scripts\build.ps1
```

This script:

1. Loads the VS Build Tools x64 environment (`cl` + SDK)
2. Configures with CMake preset **default** (Ninja, Debug, `cl`)
3. Builds
4. Runs `ctest`
5. Runs `demos/minimal_pipeline.exe`

Other options:

```powershell
.\scripts\build.ps1 -Clean              # wipe build dir first
.\scripts\build.ps1 -Config Release
.\scripts\build.ps1 -NoTest
```

### Manual path

```powershell
. .\scripts\dev_env.ps1                 # import MSVC + SDK into this shell

cmake --preset default
cmake --build --preset default
ctest --preset default

.\build\demos\minimal_pipeline.exe
.\build\tests\smoke_test.exe
```

Expected demo output:

```text
node_engine bootstrap ok
C++20 + Taskflow hello demo
```

### VS Code

- Recommended extensions: **C/C++**, **CMake Tools** (see `.vscode/extensions.json`).
- Default build task runs `scripts/build.ps1` (MSVC).
- Prefer CMake preset **default** / **release**.
- Always ensure configure uses **`cl`**, not `clang++`.

---

## Common pitfalls

| Symptom | Cause | Fix |
|---|---|---|
| `could not open 'kernel32.lib'` | No Windows SDK, or Clang without SDK env | Install **Windows 10 SDK**; use `.\scripts\build.ps1` or `dev_env.ps1` |
| CMake identifies **Clang** | LLVM on `PATH` wins | `-DCMAKE_CXX_COMPILER=cl`, delete `build/`, use build script |
| `winget install BuildTools` “no upgrade” | Package already installed | **Modify** install to add SDK/workload |
| `setup.exe ... --wait` → exit 87 | Unknown option on Installer 4.10 | Drop `--wait` |
| `setup.exe ... --passive` → exit 5007 | Not elevated | **Run as Administrator** |
| `g++` / WinLibs missing | Not required | Stick to MSVC for this project |
| FetchContent / git errors | No network or no git | Install Git; allow github.com access once for Taskflow |

---

## Dependencies

| Dependency | How it arrives |
|---|---|
| **Taskflow** | CMake `FetchContent` in `cmake/Dependencies.cmake` (pinned tag) |
| Threads | CMake `Threads` / system |
| JSON, Boost, TBB, vcpkg packages | **Not** used in core |

No separate package manager step is required after the compiler/SDK/CMake/Ninja install.

---

## Development roadmap (high level)

1. ~~Bootstrap repo (CMake, Taskflow, hello demo)~~ **done**
2. Core types: value / pin / node / graph / factory / `REGISTER_NODE`
3. Type check, propagate, autoconvert, pin literals
4. Boundary + `GraphNode` + clone + splice flatten
5. Full Taskflow dirty scheduling + `ParallelHint` slices
6. Temporal gates (coalesce / de-coalesce) + sub-ticks
7. Trigger queue + event-style node demos
8. Optional sugar later (see specs)

Track detail and acceptance criteria in [docs/initial-specs.md](docs/initial-specs.md).

---

## Documentation map

| Doc | Purpose |
|---|---|
| [README.md](README.md) | Onboarding, install, first build |
| [BUILD.md](BUILD.md) | Short build commands |
| [docs/initial-specs.md](docs/initial-specs.md) | Architecture, node author surface, roadmap, constraints |
| [LICENSE](LICENSE) | MIT |

---

## License

MIT — see [LICENSE](LICENSE).
