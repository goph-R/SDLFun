@echo off
echo Compiling Bullet Linear Math...
C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o bl.o
if errorlevel 1 goto error
echo Compiling Bullet Collision...
C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o bc.o
if errorlevel 1 goto error
echo Compiling Bullet Dynamics...
C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o bd.o
if errorlevel 1 goto error
echo Compiling main...
C:\Dev-Cpp\bin\g++.exe -Ivendor\include -Ivendor\bullet3-3.25\src -O2 -c main.cpp -o main.o
if errorlevel 1 goto error
echo Linking...
C:\Dev-Cpp\bin\g++.exe main.o bl.o bc.o bd.o -o SDLFun.exe -Lvendor\lib -lmingw32 -lSDLmain -lSDL -lopengl32 -lfmod
if errorlevel 1 goto error
echo Done! Run SDLFun.exe
goto end
:error
echo Build failed!
pause
:end
