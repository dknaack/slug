#!/bin/sh

case "$(uname)" in
	Darwin)
		cc -g3 -Wall -Wno-unused \
			-fsanitize-trap -fsanitize-undefined-trap-on-error -fsanitize=undefined,address \
			-o main main.c `pkg-config --cflags --libs glfw3` \
			-framework OpenGL -framework CoreText -framework CoreFoundation
		;;
	*)
		cc -g3 -Wall -Wno-unused -o main main.c `pkg-config --cflags --libs glfw3` -lGL
		;;
esac
