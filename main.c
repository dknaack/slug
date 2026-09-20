#include <GLES3/gl3.h>
#include <assert.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <hb.h>
#include <math.h>
#include <stdio.h>

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

static GLuint create_shader(const char *source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("Shader error: %s\n", log);
    }

    return shader;
}

static void render(void)
{
	static int is_initialized = 0;
	static hb_font_t *hb_font = NULL;
	static font_texture font = {0};
	static GLuint program = 0;

	if (!is_initialized) {
		is_initialized = 1;

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		char *font_path = "fonts/OpenSans-Regular.ttf";
		str font_file = read_file(font_path);
		font = new_font_texture(font_file);

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
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(glyph), (void *)offsetof(glyph, point_offset));
		glEnableVertexAttribArray(2);
		glVertexAttribDivisor(2, 1);
		glVertexAttribIPointer(3, 1, GL_INT, sizeof(glyph), (void *)offsetof(glyph, contour_offset));
		glEnableVertexAttribArray(3);
		glVertexAttribDivisor(3, 1);
		glVertexAttribIPointer(4, 1, GL_INT, sizeof(glyph), (void *)offsetof(glyph, contour_count));
		glEnableVertexAttribArray(4);
		glVertexAttribDivisor(4, 1);

		str vertex_shader_source = read_file("vert.glsl");
		str fragment_shader_source = read_file("frag.glsl");
		GLuint vertex_shader = create_shader(vertex_shader_source.at, GL_VERTEX_SHADER);
		GLuint fragment_shader = create_shader(fragment_shader_source.at, GL_FRAGMENT_SHADER);

		program = glCreateProgram();
		glAttachShader(program, vertex_shader);
		glAttachShader(program, fragment_shader);
		glLinkProgram(program);

		int success = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &success);
		if (!success) {
			char info_log[1024] = {0};
			glGetProgramInfoLog(program, sizeof(info_log) - 1, NULL, info_log);
			fprintf(stderr, "Failed to compile shader: %s\n", info_log);
			return;
		}

		glUseProgram(program);
		glUniform1i(glGetUniformLocation(program, "point_data"), 0);
		glUniform1i(glGetUniformLocation(program, "contour_data"), 1);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, font.point_texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, font.contour_texture);

		hb_blob_t *blob = hb_blob_create_from_file(font_path);
		hb_face_t *face = hb_face_create(blob, 0);
		hb_font = hb_font_create(face);
	}

	//
	// Render
	//

	int viewport_width, viewport_height;
	emscripten_get_canvas_element_size("#canvas", &viewport_width, &viewport_height);
	glViewport(0, 0, viewport_width, viewport_height);
	glClear(GL_COLOR_BUFFER_BIT);

	const char *text = "Hello, world!";
	float font_size = 128.0f;
	glyph glyphs[64] = {0};
	uint32_t glyph_count = 0;

	hb_buffer_t *buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, text, -1, 0, -1);
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buffer, hb_language_from_string("en", -1));
	hb_shape(hb_font, buffer, NULL, 0);

	hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
	hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

	double text_width = 0.0;
	for (unsigned int i = 0; i < glyph_count; i++) {
		double advance_x = glyph_pos[i].x_advance;
		text_width += advance_x;
	}

	hb_face_t *face = hb_font_get_face(hb_font);
	double upem = hb_face_get_upem(face);
	double scale_x = font_size / viewport_width;
	double scale_y = font_size / viewport_height;
	double cursor_x = 0.0;
	double cursor_y = 0.0;
	for (unsigned int i = 0; i < glyph_count; i++) {
		hb_codepoint_t glyph_index = glyph_info[i].codepoint;
		double offset_x = glyph_pos[i].x_offset;
		double offset_y = glyph_pos[i].y_offset;
		double advance_x = glyph_pos[i].x_advance;
		double advance_y = glyph_pos[i].y_advance;

		glyphs[i] = font.glyphs[glyph_index];
		glyphs[i].pos[0] = (glyphs[i].pos[0] + (cursor_x + offset_x - 0.5 * text_width) / upem) * scale_x;
		glyphs[i].pos[1] = (glyphs[i].pos[1] + (cursor_y + offset_y) / upem) * scale_y;
		glyphs[i].size[0] *= scale_x;
		glyphs[i].size[1] *= scale_y;

		cursor_x += advance_x;
		cursor_y += advance_y;
	}

	glBufferSubData(GL_ARRAY_BUFFER, 0, glyph_count * sizeof(*glyphs), glyphs);

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "point_data"), 0);
	glUniform1i(glGetUniformLocation(program, "contour_data"), 1);
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, glyph_count);

	emscripten_webgl_commit_frame();
}

int main(void)
{
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.enableExtensionsByDefault = 1;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context =
		emscripten_webgl_create_context("#canvas", &attrs);
    emscripten_webgl_make_context_current(context);

    emscripten_set_main_loop(render, 0, 1);
    return 0;
}
