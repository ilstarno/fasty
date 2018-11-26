@echo off

echo "Making fasty_application...""

g++ fasty_application.cpp ^
..\library\fasty_engine.c ..\library\fasty_core.c ^
-s -O3 ^
-I . -I ..\library ^
-lws2_32 -lpsapi ^
-o ..\bin\fasty_application ^
-static
