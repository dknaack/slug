#!/bin/sh

emcc main.c -g -O0 \
  -s WASM=1 \
  -s STACK_SIZE=1048576 \
  -s ASSERTIONS=1 \
  -s USE_HARFBUZZ=1 \
  -s USE_WEBGL2=1 \
  -s FULL_ES3=1 \
  -s MIN_WEBGL_VERSION=2 \
  -s MAX_WEBGL_VERSION=3 \
  -s ALLOW_MEMORY_GROWTH=1 \
  --preload-file vert.glsl \
  --preload-file frag.glsl \
  --preload-file fonts/OpenSans-Regular.ttf \
  -o main.js
