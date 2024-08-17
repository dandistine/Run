@echo off

call C:\Users\dandistine\libraries\emsdk\emsdk_env.bat
call em++ -std=c++17 -O3 -s ALLOW_MEMORY_GROWTH=1 -s MAX_WEBGL_VERSION=2 -s MIN_WEBGL_VERSION=2 -s USE_LIBPNG=1 -s SINGLE_FILE --shell-file basic_template.html Run/main.cpp -I Run -o run.html