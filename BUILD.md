# Build cheat-sheet

Onboarding + full system install: **[README.md](README.md)**  
Architecture / roadmap: **[docs/initial-specs.md](docs/initial-specs.md)**

## Toolchain (Windows)

- CMake ≥ 3.20, Ninja
- VS 2022 Build Tools: **MSVC v143** + **Windows 10 SDK**
- Primary compiler: **`cl`** (not Clang)

## One-shot

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -Clean
.\scripts\build.ps1 -Config Release
```

## Manual

```powershell
. .\scripts\dev_env.ps1
cmake --preset default
cmake --build --preset default
ctest --preset default
.\build\demos\minimal_pipeline.exe
```

## Rules of thumb

- Configure only after `dev_env.ps1` / VsDevCmd so `WindowsSdkDir` is set.
- If CMake picks Clang, force `-DCMAKE_CXX_COMPILER=cl` and delete `build/`.
- Taskflow comes from FetchContent — no vcpkg step.
