@echo off

setlocal
set path=%path%;C:/msys64/mingw32/bin

c++ "lolz.cpp" -o lolz.exe -std=c++17 -m32 -lmingw32 -luser32 -lkernel32 -lole32 -lpsapi -fopenmp -static && lolz.exe