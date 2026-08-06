#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>

typedef struct {
	char *at;
	size_t length;
} str;

static str read_file(char *path)
{
	str result = {0};

	FILE *file = fopen(path, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		result.length = ftell(file);
		fseek(file, 0, SEEK_SET);

		result.at = malloc(result.length);
		fread(result.at, 1, result.length, file);
	}

	return result;
}

int main(void)
{
	str file = read_file("fonts/OpenSans-Regular.ttf");
	if (!file.at) {
		return -1;
	}

    if (!glfwInit()) {
		return -1;
	}

    GLFWwindow *window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
