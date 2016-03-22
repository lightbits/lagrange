@echo off
if not exist "bin" mkdir bin
pushd bin
cl -nologo -Oi -Od -Zi -MD ../game.cpp -I"C:/Programming/sdl/include" /link -out:iarc.exe -subsystem:console -debug SDL2.lib SDL2main.lib opengl32.lib
popd
bin\iarc.exe
