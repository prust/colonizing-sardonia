sardoniamake:
ifeq ($(OS),Windows_NT)
	gcc -o sardonia.exe sardonia.c -I /c/msys64/usr/lib/sdl2/x86_64-w64-mingw32/include/SDL2 -L /c/msys64/usr/lib/sdl2/x86_64-w64-mingw32/lib -lmingw32 -lSDL2main -lSDL2
else
	gcc -o sardonia sardonia.c -I/usr/local/include/SDL2 -L/usr/local/lib -lSDL2
endif

sardoniadebug:
	gcc -g -o sardonia sardonia.c -I/usr/local/include/SDL2 -L/usr/local/lib -lSDL2