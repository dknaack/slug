#define GLFW_INCLUDE_NONE
#include <assert.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

#define GLAD_GL_IMPLEMENTATION
#include "glad.h"

#define TAG(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))
#define MAX(a, b) ((a) > (b)? (a) : (b))
#define MIN(a, b) ((a) < (b)? (a) : (b))
#define LENGTH(x) (sizeof(x)/sizeof((x)[0]))

#define read_i64(r) ((int64_t)read_u64(r))
#define read_i32(r) ((int32_t)read_u32(r))
#define read_i16(r) ((int16_t)read_u16(r))
#define read_i8(r) ((int8_t)read_u8(r))

typedef struct {
	char *at;
	size_t length;
} str;

typedef struct {
	str input;
	size_t pos;
} reader;

#include "font.c"

static GLuint create_shader(const char *src, GLenum type)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	int success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char info_log[1024] = {0};
		glGetShaderInfoLog(shader, sizeof(info_log) - 1, NULL, info_log);
		fprintf(stderr, "Failed to compile shader: %s\n", info_log);
		return 0;
	}

	return shader;
}

int main(void)
{
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

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	str font_file = read_file("fonts/OpenSans-Regular.ttf");
	font_texture font = new_font_texture(font_file);

	GLuint vertex_array;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	GLuint instance_buffer;
	glGenBuffers(1, &instance_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
	glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(glyph), NULL, GL_DYNAMIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glyph), (void *)offsetof(glyph, pos));
	glEnableVertexAttribArray(0);
	glVertexAttribDivisor(0, 1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glyph), (void *)offsetof(glyph, size));
	glEnableVertexAttribArray(1);
	glVertexAttribDivisor(1, 1);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(glyph), (void *)offsetof(glyph, point_offset));
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(glyph), (void *)offsetof(glyph, contour_offset));
	glEnableVertexAttribArray(3);
	glVertexAttribDivisor(3, 1);
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(glyph), (void *)offsetof(glyph, contour_count));
	glEnableVertexAttribArray(4);
	glVertexAttribDivisor(4, 1);

	str vertex_shader_source = read_file("vert.glsl");
	str fragment_shader_source = read_file("frag.glsl");
	GLuint vertex_shader = create_shader(vertex_shader_source.at, GL_VERTEX_SHADER);
	GLuint fragment_shader = create_shader(fragment_shader_source.at, GL_FRAGMENT_SHADER);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	int success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		char info_log[1024] = {0};
		glGetProgramInfoLog(program, sizeof(info_log) - 1, NULL, info_log);
		fprintf(stderr, "Failed to compile shader: %s\n", info_log);
		return -1;
	}

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "point_data"), 0);
	glUniform1i(glGetUniformLocation(program, "contour_data"), 1);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, font.point_texture);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, font.contour_texture);

	float font_size = 128.0f;
	CFDataRef data = CFDataCreate(NULL, (uint8_t *)font_file.at, font_file.length);
	CTFontDescriptorRef descriptor = CTFontManagerCreateFontDescriptorFromData(data);
	CTFontRef font_ref = CTFontCreateWithFontDescriptor(descriptor, font_size, NULL);

	CFStringRef text = CFSTR("Hello, world!");
	CFMutableAttributedStringRef string =
		CFAttributedStringCreateMutable(NULL, 0);
	CFAttributedStringReplaceString(string, CFRangeMake(0, 0), text);
	CFAttributedStringSetAttribute(string,
		CFRangeMake(0, CFStringGetLength(text)),
		kCTFontAttributeName, font_ref);

	CTLineRef line = CTLineCreateWithAttributedString(string);
	CFArrayRef runs = CTLineGetGlyphRuns(line);

	while (!glfwWindowShouldClose(window)) {
		int viewport_width, viewport_height;
		glfwGetFramebufferSize(window, &viewport_width, &viewport_height);
		glViewport(0, 0, viewport_width, viewport_height);
		glClear(GL_COLOR_BUFFER_BIT);

		glyph glyphs[64] = {0};
		uint32_t glyph_count = 0;
		for (CFIndex i = 0; i < CFArrayGetCount(runs); i++) {
			CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, i);
			CFIndex run_glyph_count = CTRunGetGlyphCount(run);
			const CGGlyph *glyph_indices = CTRunGetGlyphsPtr(run);
			const CGPoint *positions = CTRunGetPositionsPtr(run);
			for (CFIndex j = 0; j < run_glyph_count; j++) {
				CGGlyph glyph_index = glyph_indices[j];
				glyph *g = &glyphs[glyph_count++];
				*g = font.glyphs[glyph_index];

				g->pos[0] = (g->pos[0] * font_size + positions[j].x) / viewport_width;
				g->pos[1] = (g->pos[1] * font_size + positions[j].y) / viewport_height;
				g->size[0] *= font_size / viewport_width;
				g->size[1] *= font_size / viewport_height;
			}
		}

		glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(*glyphs), glyphs);

		glUseProgram(program);
		glUniform1i(glGetUniformLocation(program, "point_data"), 0);
		glUniform1i(glGetUniformLocation(program, "contour_data"), 1);

		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, glyph_count);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}
