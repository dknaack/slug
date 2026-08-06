#!/bin/sh

case "$(uname)" in
	Darwin)
		cc -g3 -Wall -o main main.c `pkg-config --cflags --libs glfw3` -framework OpenGL
		;;
	*)
		cc -g3 -Wall -o main main.c `pkg-config --cflags --libs glfw3` -lGL
		;;
esac
