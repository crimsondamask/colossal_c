@REM Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.
@set OUT_DIR=build
@set OUT_EXE=colossal
@set INCLUDES=/Iimgui /Iimgui/GLFW /Ilibmodbus
@set SOURCES=colossal.cpp imgui\imgui_impl_glfw.cpp imgui\imgui_impl_opengl3.cpp imgui\imgui*.cpp
@set LIBS=/LIBPATH:libs glfw3.lib opengl32.lib gdi32.lib shell32.lib
mkdir %OUT_DIR%
cl /nologo /Zi /MD /utf-8 %INCLUDES% %SOURCES% /Fe%OUT_DIR%/%OUT_EXE%.exe /Fo%OUT_DIR%/ /link %LIBS%
