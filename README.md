## Game name: lagrange

![](img/gameplay1.gif)

## Compilation

First step is to acquire SDL

Then

    > WINDOWS
    > vcvarsall
    > cl -nologo -Oi -Od -Zi -MD ../game.cpp -I"<your sdl root>/include" /link -out:iarc.exe -subsystem:console -debug SDL2.lib SDL2main.lib opengl32.lib

    $ LINUX
    $ mkdir bin
    $ cd bin
    $ g++ ../game.cpp -o game -lGL `sdl2-config --cflags --libs`
