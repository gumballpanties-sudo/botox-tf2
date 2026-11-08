# Building botox (TF2) from source

This guide takes you from a clean Windows PC to a working `botox.dll`. You do not need to know how to code. You will copy commands into a terminal and press Enter.

Total time: about 30 to 60 minutes, most of it downloads. The build itself takes 1 to 2 minutes.

What you end up with: `build\Release\botox.dll` (about 12 MB).

---

## 1. Install the tools (one time)

You need three things:

| Tool | Why |
|---|---|
| Visual Studio 2022 Community (free) | the C++ compiler |
| Git | downloads source code |
| CMake | turns the project into something Visual Studio can build |

Easiest way: open **PowerShell** (press Start, type `powershell`, press Enter) and paste these three lines one at a time:

```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e
winget install --id Microsoft.VisualStudio.2022.Community -e --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive"
```

The last one is big (several GB) and shows a Visual Studio installer window. Let it finish.

**Doing it by hand instead?** Download Visual Studio 2022 Community and, in the installer, tick the **"Desktop development with C++"** box. Keep the default options on the right side (they include the Windows SDK, which the project needs). Then install Git from git-scm.com and CMake from cmake.org.

When everything is installed, **restart your PC** (or at least close every terminal) so Windows picks up the new tools.

> Visual Studio 2022 is the version this project is tested with. Newer versions (2026) will probably work but have not been tested.

---

## 2. Open the right terminal

Press Start, type **`Developer PowerShell for VS 2022`**, and open it.

Use this terminal for every step below. It already knows where the compiler and CMake are, so things "just work".

Check it worked. Each line should print a version number, not an error:

```powershell
git --version
cmake --version
```

---

## 3. Download the source code

Pick a folder to keep things in. This guide uses `C:\dev`.

```powershell
mkdir C:\dev
cd C:\dev
git clone https://github.com/gumballpanties-sudo/botox-tf2.git
cd botox-tf2
```

If `git clone` asks for a login or says "repository not found", the repo is private. Ask the owner to add your GitHub account, or ask for a zip of the folder and unzip it to `C:\dev\botox-tf2`.

**From now on, every command runs inside `C:\dev\botox-tf2`.** If you close the terminal, open it again (step 2) and run `cd C:\dev\botox-tf2` first.

---

## 4. Get the FreeType library (one time)

botox uses a font library called FreeType. The repo contains its header files but not the compiled library file (`freetype.lib`), so you build it once with **vcpkg**, Microsoft's library downloader.

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg.exe install "freetype[core]:x64-windows-static"
```

Then copy the result into the project:

```powershell
robocopy C:\vcpkg\installed\x64-windows-static vcpkg_local\x64-windows-static /E
```

robocopy prints a summary table when it is done. That is normal, it is not an error.

Notes:
- Type `freetype[core]` exactly as shown, with the brackets. Plain `freetype` pulls in extra libraries (png, zlib, brotli) that botox does not link, and the build then fails with "unresolved external symbol" errors.
- If `C:\vcpkg` already exists on your PC, skip the `git clone` and `bootstrap` lines and just run the `install` and `robocopy` lines.

Check it worked:

```powershell
dir vcpkg_local\x64-windows-static\lib\freetype.lib
```

You should see the file listed (about 6 to 7 MB).

---

## 5. Build botox

Two commands. The first one prepares the project (only needed once). The second one compiles.

```powershell
cmake -B build -A x64
cmake --build build --config Release --target botox
```

The second command prints a lot of text. That is normal. It is done when you see a line ending in:

```
botox.vcxproj -> C:\dev\botox-tf2\build\Release\botox.dll
```

Your DLL is at **`C:\dev\botox-tf2\build\Release\botox.dll`**.

At runtime botox keeps its files in `C:\botox-tf2\` (`default.cfg` for settings, `debug.log` for diagnostics).

---

## 6. Rebuilding later

After you pull new code (`git pull`) or edit something, only run:

```powershell
cmake --build build --config Release --target botox
```

Run `cmake -B build -A x64` again only if a new `.cpp` file was added to `CMakeLists.txt`.

**Close TF2 before rebuilding.** While the game has the DLL loaded, Windows locks the file and the build cannot replace it.

---

## Troubleshooting

| You see | Fix |
|---|---|
| `'cmake' is not recognized` or `'git' is not recognized` | You are not in Developer PowerShell (step 2), or you did not restart after installing. |
| `could not find any instance of Visual Studio` | The "Desktop development with C++" workload is missing. Open "Visual Studio Installer", click **Modify** on VS 2022, tick it, install. |
| `freetype.lib missing from vcpkg_local/...` | Step 4 was skipped or the `robocopy` line was not run from inside `C:\dev\botox-tf2`. |
| `unresolved external symbol` mentioning `png`, `deflate`, `BrotliDecoder` or `BZ2_` | FreeType was built with extras. Run `C:\vcpkg\vcpkg.exe remove freetype:x64-windows-static`, then redo step 4 with `freetype[core]` exactly. |
| `LNK1104: cannot open file '...botox.dll'` | TF2 (or an injector) still has the DLL open. Close TF2 and build again. |
| `Error: generator ... does not match the generator used previously` | You switched Visual Studio versions. Delete the `build` folder (`rmdir /s /q build` in cmd, or delete it in Explorer) and redo step 5. |
| `Cannot find source file: tests/...` | Your copy of `CMakeLists.txt` is old. `git pull` to get the current one. |
| Anything else | Copy the **first** line that contains `error` (not the last one) and send it to the owner. The first error is the real one; the rest are usually follow-on noise. |
