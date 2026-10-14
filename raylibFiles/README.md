
## Clone and initialize submodules

For a new clone:

```powershell
git clone --recurse-submodules <repository-url>
cd Tesseract
```

If you already cloned the repository:

```powershell
git submodule update --init --recursive
```

The raylib source should be present in `external/raylib/`.

## Configure and build

Run these commands from the **Tesseract repository root** (where `CMakeLists.txt` is located):

```powershell
cmake -S . -B build/raylib-test -G "Visual Studio 18 2026" -A x64
cmake --build build/raylib-test --config Release
```

## Run

```powershell
.\build\raylib-test\bin\Release\program.exe
```

The application should open the raylib window defined in `src/newplace.cpp`.

## Rebuild after code changes

```powershell
cmake --build build/raylib-test --config Release
```

## Clean build

To reset the generated files and CMake cache for the standalone raylib build:

```powershell
Remove-Item -Recurse -Force build/raylib-test
cmake -S . -B build/raylib-test -G "Visual Studio 18 2026" -A x64
cmake --build build/raylib-test --config Release
```

## Project layout

```text
Tesseract/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── .gitmodules
├── src/
│   ├── main.cpp          # Head-tracking application (not built in this configuration)
│   └── newplace.cpp      # Standalone raylib program
├── external/
│   ├── opencv/
│   ├── opencv_contrib/
│   └── raylib/            # Git submodule
└── build/                 # Generated, ignored
```

## Troubleshooting

**`not a CMake build directory (missing CMakeCache.txt)`**  
Use `cmake --build build/raylib-test --config Release`, not `cmake --build build`.

**`could not find any instance of Visual Studio`**  
Confirm Visual Studio 2026 and its C++ workload are installed. Run `cmake --help` to confirm your version of CMake supports the `Visual Studio 18 2026` generator.

**`external/raylib` does not contain source files**  
Run `git submodule update --init --recursive`.

**`CMakeLists.txt` not found**  
Run the commands from the repository root.

## Next step

After the standalone raylib program works, raylib can be linked into the main Tesseract executable alongside OpenCV and dlib.
