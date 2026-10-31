@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "C:\Users\huber\Desktop\SimpleVisual\cplus\tools\chblend_test\.build"
cl /nologo /std:c++17 /O2 /EHsc /W0 /DNOMINMAX ^
   /I"C:\Users\huber\Desktop\SimpleVisual\cplus\tools\chblend_test\stubs" /I"C:\Users\huber\Desktop\SimpleVisual\cplus\tools\chblend_test\.krita-428d44705de20770434ea2915021241dc073879c" /I"C:\Users\huber\Desktop\SimpleVisual\cplus\src\features" ^
   "C:\Users\huber\Desktop\SimpleVisual\cplus\tools\chblend_test\chblend_test.cpp" /Fe:chblend_test.exe d3d9.lib user32.lib || exit /b 1
