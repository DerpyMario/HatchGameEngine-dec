@ECHO OFF
SET OBJ_FOLDER=out\3ds\
IF NOT EXIST %OBJ_FOLDER% MKDIR %OBJ_FOLDER%

CD "tools"
"makeheaders.exe" ../source
CD ..

make -f Makefile.3ds
if NOT %errorlevel% == 0 goto :FINISHED

COPY /Y "builds\3ds\HatchGameEngine.3dsx" "Z:\3ds\Homebrew\HatchGameEngine.3dsx"
C:\devkitPro\tools\bin\3dslink.exe builds/3ds/HatchGameEngine.3dsx -a 192.168.1.100

:FINISHED
pause
