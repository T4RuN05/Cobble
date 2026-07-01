# Xournal++ Repository Architecture Master Context

**Target Audience:** AI Agents & Developer Onboarding
**Purpose:** Comprehensive architectural mapping, codebase navigation, and local setup guide.

---

## 1. Project Overview & Core Architecture
Xournal++ is a C++ application utilizing GTK3 for its graphical interface. The entire codebase strictly follows a **Model-View-Controller (MVC)** architectural pattern, separating document data, physical rendering, and user input.

### 1.1 Model (Data Structures & State)
- **`src/core/model/`**: The core data structures of a `.xopp` file. Includes `Document.cpp` (root container), `Stroke.cpp` (pressure-sensitive vector arrays), `XojPage.cpp` (pages), and `Element.cpp` (base for images, text, rendering). Contains the `eraser/` subdirectory which uses a spatial `ErasableStrokeOverlapTree` to calculate intersection points for partial stroke erasing.
- **`src/core/undo/`**: The Command Pattern engine. Contains over 50 specific `UndoAction` classes (e.g., `ScaleUndoAction`, `RotateUndoAction`) managed by the `UndoRedoHandler.cpp` stack to safely reverse document mutations without file corruption.
- **`src/core/enums/`**: Application-wide C++ `enum classes` (`ActionType`, `StrokeType`, `Button`) for strict type safety.

### 1.2 View (Rendering & UI Definitions)
- **`src/core/view/`**: Uses `cairo` to physically draw model elements onto the GTK canvas. Handles `background/` layers (ruled, isometric, staves) and `overlays/` (selection boxes, laser pointers). Key files: `StrokeView.cpp`, `DocumentView.cpp`.
- **`ui/`**: Static GTK XML framework. Contains `.glade` and `.ui` XML files (`settings.glade`, `main.glade`), `xournalpp.css` for custom styling, and `mainmenubar.xml`. It also houses over 250 SVG/PNG icons (`iconsLucide-light`, etc.).

### 1.3 Controller (User Input & GTK Events)
- **`src/core/control/`**: The central brain. `Control.cpp` (127KB) bridges GTK events to the `.xopp` internal state (opening, saving, scrolling). `ToolHandler.cpp` maps stylus/mouse inputs to specific tools (Pen, Eraser). `XournalMain.cpp` is the bootstrap entry point.
- **`src/core/gui/`**: Over 90 C++ files that parse the `ui/` XML sheets at runtime and attach event listeners to sidebars, toolbars, and specific dialogs (`ExportDialog`, `TexDialog`).

---

## 2. Subsystems & Extensions

### 2.1 PDF Handling & Exporting
- **`src/core/pdf/`**: Deep abstraction layer for PDF manipulation. `base/` defines abstract interfaces for exporting (via Cairo or QPDF). `popplerapi/` contains concrete GTK/Poppler implementations that load, parse, and jump to PDF bookmarks.

### 2.2 Audio Processing
- **`src/core/audio/`**: Hardware audio streaming via `libportaudio` (`PortAudioConsumer`). Threaded file compression/decompression into Ogg Vorbis/Opus formats (`VorbisConsumer`). Passes float arrays via a thread-safe `AudioQueue`.

### 2.3 Lua Plugin Infrastructure
- **`plugins/`**: Out-of-the-box Lua extensions (e.g., `ColorCycle`, `FitToContent`, `QuickScreenshot`). Each contains a `main.lua` script and `plugin.ini` metadata. Includes `luapi_application.def.lua` for IDE autocomplete.
- **`src/core/plugin/`**: The Lua-C++ bridge. `PluginController.cpp` manages the Lua lifecycle, using a massive 150KB header `luapi_application.h` to expose internal Xournal++ C++ methods to Lua scripts.

---

## 3. Auxiliary Modules & Tooling

- **`src/util/`**: Over 80 decoupled C++ helper files. Handles math, binary `.xopp` serialization streams (`ObjectOutputStream.h`), string processors, and memory safety (GTK/Cairo RAII wrappers).
- **`src/exe/`**: OS-specific executable wrappers (`osx/setup-env.cpp`, `win32/console.cpp`) that initialize OS hooks before calling the `Control` hub.
- **`src/xoj-preview-extractor/`**: A standalone C++ CLI tool (`xournalpp-thumbnailer.cpp`) deployed to operating systems (like GNOME) to extract `.png` thumbnails from `.xopp` files for native file explorers.
- **`test/`**: Automated C++ unit test suite (testing file loads, stroke splitting) utilizing a `files/` payload directory of dummy `.xopp` and `.pdf` files.
- **`development/`**: Developer-specific documentation (e.g., math theory behind the Eraser tool in actual `.xopp` files) and Valgrind GTK memory leak suppressions.

---

## 4. Package Management, CI/CD & Assets

- **`.github/`**: Over 20 GitHub Actions YAML workflows to auto-compile, test, lint (`.clang-format`), and release binaries for Ubuntu/macOS/Windows.
- **OS Integrations (`desktop/`, `mac-setup/`, `linux-setup/`, `rpm/`, `debian/`)**: Scripts and metadata to bundle the compiled binary into installable `.deb`, `.rpm`, `.AppImage`, or `.dmg` packages.
- **Translations (`po/`)**: Contains `xournalpp.pot` base template and over 30 `.po` dictionaries for localization via `gettext`.
- **Root Configurations**: `CMakeLists.txt` (core build logic), `.clang-format` (styling rules), `Doxyfile` (C++ docs generation), `LICENSE`.
- **Resources (`resources/`, `palettes/`, `readme/`)**: LaTeX `default_template.tex` boilerplate, default `.gpl` hex color palettes, and markdown build guides.

---

## 5. Windows-Specific Setup and Build Guide (MSYS2)

To compile, test, and package Xournal++ on **Windows 10/11** using MSYS2 and MinGW-w64:

### 5.1 Install Build Dependencies
1. Install [MSYS2](https://www.msys2.org/) (Use a short path without spaces like `C:\msys64`).
2. Install [NSIS](https://nsis.sourceforge.io/Download) (required for generating the final `.exe` installer).

### 5.2 Update MSYS2 Base Environment
Open an **MSYS2 console** (not MINGW64) and run the following command to update base packages. Restart the console and run it a second time.
```sh
pacman -Syuu
```

### 5.3 Install Toolchain and Compilers
Open a **MINGW64 console** (Ensure the prompt says `MINGW64`).
```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-imagemagick mingw-w64-x86_64-gettext patch make git
```

### 5.4 Install Xournal++ Required Libraries
In the same **MINGW64 console**, install all UI, audio, and PDF dependencies:
```sh
pacman -S mingw-w64-x86_64-poppler mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libsndfile mingw-w64-x86_64-libzip mingw-w64-x86_64-lua mingw-w64-x86_64-portaudio mingw-w64-x86_64-gtksourceview4 mingw-w64-x86_64-qpdf
```

### 5.5 Compilation
Navigate to the cloned repository folder (e.g., `cd /c/Users/tarun/Desktop/Github\ Repo/xournalpp`).
```sh
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### 5.6 Packaging for Windows (Installer Generation)
Xournal++ relies on an NSIS script (`windows-setup/xournalpp.nsi`) to generate the Windows executable installer.
```sh
# Ensure you are at the root directory of the repo
./windows-setup/package.sh build/
```
- A portable version of Xournal++ is generated inside `build/dist/`.
- The installable Windows Setup file (`xournalpp-setup.exe`) is generated inside the `build/` folder.
