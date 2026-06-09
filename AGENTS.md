# Supercell Wx AI Agent Instructions

Supercell Wx is a cross-platform C++20/Qt6 application for visualizing live and archived NEXRAD weather radar data. This guide helps AI agents understand the codebase architecture and development workflow.

## Project Architecture

### Two-Library Structure
- **wxdata/** - Core radar data processing library (platform-independent, no Qt)
  - Parses NEXRAD Level 2/3 files ([wxdata/include/scwx/wsr88d/](wxdata/include/scwx/wsr88d/))
  - Network providers for AWS/NWS data, Blitzortung lightning, GFS model data, and IEM API ([wxdata/include/scwx/provider/](wxdata/include/scwx/provider/))
  - AWIPS message parsing ([wxdata/include/scwx/awips/](wxdata/include/scwx/awips/))
  - GR placefile support ([wxdata/include/scwx/gr/](wxdata/include/scwx/gr/))
  - GRIB2 data parsing ([wxdata/include/scwx/grib2/](wxdata/include/scwx/grib2/))
  - Sounding data ([wxdata/include/scwx/sounding/](wxdata/include/scwx/sounding/))
  - SPC outlook/mesoscale discussion data ([wxdata/include/scwx/spc/](wxdata/include/scwx/spc/))
  - Common types and utilities ([wxdata/include/scwx/common/](wxdata/include/scwx/common/), [wxdata/include/scwx/types/](wxdata/include/scwx/types/))
  - Network utilities ([wxdata/include/scwx/network/](wxdata/include/scwx/network/))
  - Zip utilities ([wxdata/include/scwx/zip/](wxdata/include/scwx/zip/))
  - Uses shared Conan dependencies but NO Qt

- **scwx-qt/** - Qt GUI application layer
  - Main window and UI ([scwx-qt/source/scwx/qt/main/](scwx-qt/source/scwx/qt/main/))
  - Manager classes coordinate application state ([scwx-qt/source/scwx/qt/manager/](scwx-qt/source/scwx/qt/manager/))
  - Map rendering with MapLibre GL ([scwx-qt/source/scwx/qt/map/](scwx-qt/source/scwx/qt/map/))
  - OpenGL drawing primitives ([scwx-qt/source/scwx/qt/gl/](scwx-qt/source/scwx/qt/gl/))
  - Product views connect data to visualization ([scwx-qt/source/scwx/qt/view/](scwx-qt/source/scwx/qt/view/))
  - Python build helpers for code generation ([scwx-qt/tools/](scwx-qt/tools/))
  - Qt translation files ([scwx-qt/ts/](scwx-qt/ts/))

**Critical:** Keep Qt code isolated to scwx-qt. Never add Qt dependencies to wxdata.

### Manager Pattern
Manager classes in [scwx-qt/source/scwx/qt/manager/](scwx-qt/source/scwx/qt/manager/) are singletons that manage global application concerns. Key managers include:
- `RadarProductManager` / `RadarProductManagerNotifier` - loads/caches radar products, emits Qt signals for data updates
- `SettingsManager` - persistent settings via QSettings
- `AlertManager` - weather alert processing
- `PlacefileManager` - external placefile integration
- `TimelineManager` - time-based product selection
- `BlitzortungManager` - lightning data processing
- `DownloadManager` - download queue management
- `FontManager` - font loading and caching
- `SoundingManager` - Sounding generation via rustwx sounding_plot binary (replaces GfsManager)
- `HotkeyManager` - keyboard shortcut configuration
- `LogManager` - application logging
- `MarkerManager` - map marker state
- `MediaManager` - media capture/export
- `PositionManager` - geographic position tracking
- `RadarSiteStatusManager` - radar site operational status
- `ResourceManager` - bundled resource loading
- `SpcMdManager` / `SpcOutlookManager` - SPC mesoscale discussion/outlook data
- `TaskManager` - background task lifecycle
- `TextEventManager` - text-based weather event parsing
- `ThreadManager` - Boost thread pool management
- `UpdateManager` - application update checks
- `WisManager` - NWS WFO alert state

Managers communicate via Qt signals/slots. When data flows from wxdata → scwx-qt, it typically goes through a manager.

### Background Task Processing
Background tasks use **Boost thread pools and ASIO post** for asynchronous operations. This includes network requests, file I/O, and data processing that shouldn't block the UI thread.

### Namespace Convention
All code uses nested `namespace scwx { namespace X { ... } }` structure:
- `scwx::wsr88d` - NEXRAD data structures
- `scwx::qt::view` - Qt view classes
- `scwx::qt::manager` - manager singletons
Use fully qualified namespaces in headers; `using namespace` prohibited in headers per Google C++ Style Guide.

## Build System

### Conan + CMake Workflow
The project uses **Conan 2** for C++ dependency management. CMake integrates Conan via `cmake-conan` provider.

**Setup script usage** (recommended path for new developers):
```powershell
# Windows (from repo root)
# These scripts automatically initialize the MSVC environment
.\tools\setup-windows-vs2026-release.bat [BUILD_DIR] [VENV_PATH]
.\tools\setup-windows-ninja-release.bat [BUILD_DIR] [VENV_PATH]

# Linux
./tools/setup-linux-ninja-release.sh [BUILD_DIR] [CONAN_PROFILE] [VENV_PATH] [ASAN_ENABLE]
```

**Building from the command line:**
On Windows, use the provided build script to ensure the MSVC environment is properly initialized:
```powershell
.\tools\build-windows.bat
```

**Manual CMake configuration:**
1. **Configure via Preset** (Modern approach):
   ```bash
   cmake --preset <preset-name>
   ```
   Presets encapsulate generator, build type, and Conan profiles. See `CMakePresets.json` for available names (e.g., `windows-vs2026-x64-ninja-release`, `linux-gcc14-release`).
   
   **Note:** When using Ninja-based presets on Windows, you must initialize the MSVC developer environment (e.g., via `VsDevCmd.bat` or by calling `.\tools\lib\setup-msvc.bat`) in your terminal before running CMake.


2. **Configure Manually** (Fallback):
   ```bash
   # 1. Install Conan profile
   conan config install ./tools/conan/profiles/<profile_name> -tf profiles

   # 2. Configure (Conan provider auto-installs deps)
   mkdir build && cd build
   cmake ../ -G Ninja \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=../external/cmake-conan/conan_provider.cmake \
     -DCONAN_HOST_PROFILE=<profile_name> \
     -DCONAN_BUILD_PROFILE=<profile_name>
   ```

3. **Build:**
   ```bash
   # On Windows (ensures environment setup)
   .\tools\build-windows.bat

   # Standard CMake build (requires manual environment setup on Windows)
   cmake --build --preset <preset-name> --target supercell-wx
   # OR manually:
   cmake --build . --target supercell-wx
   ```

**Key Conan profiles:** See [tools/conan/profiles/](tools/conan/profiles/)
- Windows: `scwx-windows_vs2026_x64`, `scwx-windows_vs2022_x64`
- Linux: `scwx-linux_gcc-[11-16]`, `scwx-linux_clang-[17-22]`
- macOS: `scwx-macos_clang-18` (x86_64), `scwx-macos_clang-[18-22]_armv8`

**CMake Presets:** Use [CMakePresets.json](CMakePresets.json) for IDE integration. Presets like `windows-vs2026-x64-ninja-release` encapsulate toolchain/profile selection.

### Python Virtual Environment
Project uses Python for code generation (counties DB, version info). Python dependency management uses **uv** with [pyproject.toml](pyproject.toml) and [uv.lock](uv.lock). Setup scripts create `.venv/` and sync dependencies via `uv sync`. **uv must be installed** as a prerequisite (install via `pip install uv` or the [official installer](https://docs.astral.sh/uv/#installation)). CMake macro `scwx_python_setup()` in [tools/scwx_config.cmake](tools/scwx_config.cmake) finds the venv Python.

### Build Outputs
Per [tools/scwx_config.cmake](tools/scwx_config.cmake), binaries go to:
- `build/<preset>/<BuildType>/bin/supercell-wx[.exe]`
- `build/<preset>/<BuildType>/lib/` for shared libraries

The current project version is **0.6.0** (set in [CMakeLists.txt](CMakeLists.txt)).

## Squad Framework

This project uses **Squad**, an AI team framework defined in `.squad/`. Before starting work:
1. Read `.squad/team.md` for the team roster, member roles, and capability profile.
2. Read `.squad/routing.md` for work routing rules.
3. If assigned a `squad:{member}` label, read that member's charter (`.squad/agents/{member}/charter.md`).

See `.github/copilot-instructions.md` for detailed Squad workflow instructions.

## External Dependencies

### Vendored vs Conan
- **Conan-managed** ([conanfile.py](conanfile.py)): Boost, cpr, fontconfig, geographiclib, GEOS, glm, GoogleTest, libcurl, libjpeg, libpng, libtiff, libxml2, libzip, OpenSSL, range-v3, re2, spdlog, SQLite3, vulkan-loader, zlib, and more (conditional: onetbb, opengl, glu on Linux)
- **Git submodules** ([external/](external/)): aws-sdk-cpp, cmake-conan, date, glad, hsluv-c, imgui, imgui-backend-qt, maplibre-native, maplibre-native-qt, qt6ct, stb, textflowcpp, units
  - Use submodules when heavy customization or unreleased versions needed
  - MapLibre is vendored because Qt bindings require custom build

### Qt 6.11.0 Requirement
Qt must be installed separately (not via Conan). Install via [Qt online installer](https://www.qt.io/download-open-source) or `aqtinstall`:
```bash
aqt install-qt windows desktop 6.11.0 win64_msvc2022_64 -m qtimageformats qtmultimedia qtpositioning qtserialport
```
Qt version compatibility: patch releases interchangeable (6.11.x), minor versions may break.

## Testing

### Google Test Framework
Tests in [test/](test/) use GTest (from Conan). Run via CMake:
```bash
cmake --build . --target wxtest
ctest --output-on-failure
```

Test files follow `*.test.cpp` naming. Example from [test/source/scwx/wsr88d/nexrad_file_factory.test.cpp](test/source/scwx/wsr88d/nexrad_file_factory.test.cpp):
```cpp
TEST(NexradFileFactory, Level2V06) {
   std::string filename = std::string(SCWX_TEST_DATA_DIR) + "/data/KLSX20240909_175655_V06";
   // ...
}
```

Test data stored in [test/data/](test/data/). `SCWX_TEST_DATA_DIR` macro defined during CMake configuration.

## Code Style & Conventions

### Formatting
- **clang-format**: Apply before commits. Config in [.clang-format](.clang-format). IDEs auto-format on save.
- **Google C++ Style Guide**: Follow naming, structure conventions (https://google.github.io/styleguide/cppguide.html)
- **Namespace style**: Nested namespaces with closing comments (see examples above)

### Pimpl Idiom
Many classes use Pimpl (Pointer to Implementation):
```cpp
// Header
class MyClass {
   class Impl;
   std::unique_ptr<Impl> p;
};

// Source
class MyClass::Impl { /* private state */ };
```
Reduces recompilation and hides implementation details.

### Prohibited Patterns
- No `using namespace` in headers
- No GPL-licensed dependencies (LGPL only if in shared libraries)
- Avoid Qt includes in wxdata/ headers

### Qt Signal/Slot Usage
**Critical:** Always use `Q_EMIT` instead of the Qt `emit` keyword due to a dependency conflict. Example:
```cpp
Q_EMIT DataReloaded();  // Correct
emit DataReloaded();    // Incorrect - will cause build issues
```

## Common Tasks

### Adding New Radar Product Support
1. Define product message structure:
   - For **NWS Level 3 Product ICD** products: use [wxdata/include/scwx/wsr88d/rpg/](wxdata/include/scwx/wsr88d/rpg/)
   - For **other product types** (custom, experimental, non-ICD): create new directory/namespace under wxdata/include/scwx/
2. Register in appropriate product factory (e.g., [wxdata/source/scwx/wsr88d/rpg/](wxdata/source/scwx/wsr88d/rpg/) for Level 3 ICD products)
3. Create view class in [scwx-qt/source/scwx/qt/view/](scwx-qt/source/scwx/qt/view/)
4. Wire to `RadarProductManager` for data loading
5. Add layer in [scwx-qt/source/scwx/qt/map/](scwx-qt/source/scwx/qt/map/) for rendering

### Updating Dependencies
Modify [conanfile.py](conanfile.py) `requires` tuple, then re-run Conan install. For Conan profile changes, edit [tools/conan/profiles/](tools/conan/profiles/).

### Cross-Platform Considerations
- Windows uses Schannel for SSL; Linux/macOS use OpenSSL (see [conanfile.py](conanfile.py) `configure()`)
- Linux requires X11/Wayland libraries (listed in [README.md](README.md))
- macOS deployment target 12.0 (set in [CMakeLists.txt](CMakeLists.txt))

## Debugging & Development

### Visual Studio Code Setup
Recommended extensions: C/C++ Extension Pack, clangd, CMake Tools, Python. 
**Windows-specific:** Launch from *x64 Native Tools Command Prompt for VS 2022/2026* or configure shortcut (see [developer-setup.rst](https://supercell-wx.readthedocs.io/en/stable/development/developer-setup.html)).

### Address Sanitizer
Enable with `-DSCWX_ADDRESS_SANITIZER=ON` or use presets like `linux-gcc14-debug-asan`. Useful for memory leak/corruption detection.

### CI Reference
See [.github/workflows/ci.yml](.github/workflows/ci.yml) for the main build matrix (Windows, Linux, macOS with various compiler/arch combos). Additional workflows:
- `.github/workflows/clang-format-check.yml` — Formatting checks
- `.github/workflows/clang-tidy-comments.yml` — Static analysis comments
- `.github/workflows/clang-tidy-review.yml` — Clang-tidy review on PRs
- `.github/workflows/sign-windows-packages.yml` — Windows code signing

CI mirrors setup scripts but includes AppImage packaging, artifact collection.

## Resources
- **Documentation:** https://supercell-wx.readthedocs.io/
- **Contributing:** [CONTRIBUTING.md](CONTRIBUTING.md)
