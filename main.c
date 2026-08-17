#define GLFW_INCLUDE_NONE
#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>

#define GLAD_GL_IMPLEMENTATION
#include "glad.h"

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
	reader r = {0};
	r.input = read_file("fonts/OpenSans-Regular.ttf");
	if (!r.input.at) {
		return -1;
	}

	char c = 'A';

	if (!glfwInit()) {
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow *window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
	if (!window) {
		return -1;
	}

	glfwMakeContextCurrent(window);

	int version = gladLoadGL(glfwGetProcAddress);
	if (version == 0) {
		return -1;
	}

	printf("Loaded OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

	const char *vertex_shader_source =
		"#version 410 core\n"
		"void main()\n"
		"{\n"
		"    const vec4 vertices[4] = vec4[4](\n"
		"        vec4(-1.0, -1.0, 0.0, 1.0),\n"
		"        vec4(+1.0, -1.0, 0.0, 1.0),\n"
		"        vec4(-1.0, +1.0, 0.0, 1.0),\n"
		"        vec4(+1.0, +1.0, 0.0, 1.0)\n"
		"    );\n"
		"\n"
		"    gl_Position = vertices[gl_VertexID];\n"
		"}\n";
	GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
	glCompileShader(vertex_shader);

	const char *fragment_shader_source =
		"#version 410 core\n"
		"out vec4 frag_color;\n"
		"void main()\n"
		"{\n"
		"	frag_color = vec4(1.0);\n"
		"}\n";
	GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
	glCompileShader(fragment_shader);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	int success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		char info_log[1024] = {0};
		glGetProgramInfoLog(program, sizeof(info_log) - 1, NULL, info_log);
		printf("Failed to compile shader: %s\n", info_log);
		return -1;
	}

	glUseProgram(program);

	GLuint vertex_array = 0;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	while (!glfwWindowShouldClose(window)) {
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}
