#define GLFW_INCLUDE_NONE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>

#define GLAD_GL_IMPLEMENTATION
#include "glad.h"

#define TAG(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))

typedef enum {
	OTF_TABLE_NONE,
	OTF_TABLE_CMAP,
	OTF_TABLE_GLYF,
	OTF_TABLE_HEAD,
	OTF_TABLE_LOCA,
	OTF_TABLE_MAXP,
	OTF_TABLE_COUNT
} otf_table_type;

typedef enum {
	OTF_GLYF_ON_CURVE_POINT = 0x01,
	OTF_GLYF_X_SHORT_VECTOR = 0x02,
	OTF_GLYF_Y_SHORT_VECTOR = 0x04,
	OTF_GLYF_REPEAT_FLAG    = 0x08,
	OTF_GLYF_X_IS_SAME      = 0x10,
	OTF_GLYF_Y_IS_SAME      = 0x20,
	OTF_GLYF_OVERLAP_SIMPLE = 0x40,
	OTF_GLYF_RESERVED       = 0x80,
} otf_glyf_flag;

typedef struct {
	uint16_t units_per_em;
	uint16_t index_to_loc_format;
} otf_head;

typedef struct {
	int16_t x, y;
} point;

typedef struct {
	point *points;
	uint32_t *contours;
	uint32_t contour_count;
	int32_t x_min, y_min;
	int32_t x_max, y_max;
} glyph;

typedef struct {
	char *at;
	size_t length;
} str;

typedef struct {
	str input;
	size_t pos;
} reader;

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

static uint32_t read_u64(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint64_t result = 0;
	result |= (uint64_t)p[r->pos++] << 56ll;
	result |= (uint64_t)p[r->pos++] << 48ll;
	result |= (uint64_t)p[r->pos++] << 40ll;
	result |= (uint64_t)p[r->pos++] << 32ll;
	result |= (uint64_t)p[r->pos++] << 24ll;
	result |= (uint64_t)p[r->pos++] << 16ll;
	result |= (uint64_t)p[r->pos++] <<  8ll;
	result |= (uint64_t)p[r->pos++] <<  0ll;
	return result;
}

static uint32_t read_u32(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint32_t result = 0;
	result |= p[r->pos++] << 24;
	result |= p[r->pos++] << 16;
	result |= p[r->pos++] <<  8;
	result |= p[r->pos++] <<  0;
	return result;
}

static uint16_t read_u16(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint16_t result = 0;
	result |= p[r->pos++] << 8;
	result |= p[r->pos++] << 0;
	return result;
}

static uint16_t read_u8(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint16_t result = p[r->pos++];
	return result;
}

static uint32_t get_glyph_index(reader *r, uint32_t codepoint)
{
	assert(r->pos == 0);

	uint32_t result = 0;
	uint16_t _version = read_u16(r);
	uint16_t num_tables = read_u16(r);

	for (uint16_t i = 0; i < num_tables; i++) {
		uint16_t _platform_id = read_u16(r);
		uint16_t _encoding_id = read_u16(r);
		uint32_t subtable_offset = read_u32(r);

		size_t orig_pos = r->pos;
		r->pos = subtable_offset;

		uint16_t format = read_u16(r);
		uint16_t length = read_u16(r);
		uint16_t language = read_u16(r);
		uint16_t segment_count_twice = read_u16(r);
		uint16_t _search_range = read_u16(r);
		uint16_t _entry_selector = read_u16(r);
		uint16_t _range_shift = read_u16(r);

		int found_segment = 0;
		int32_t segment_index = 0;
		uint16_t segment_end = 0;
		uint16_t segment_count = segment_count_twice / 2;
		for (uint16_t i = 0; i < segment_count; i++) {
			uint16_t end_code = read_u16(r);
			if (!found_segment && codepoint <= end_code) {
				segment_index = i;
				segment_end = end_code;
				found_segment = 1;
			}
		}

		r->pos += 2; // padding byte

		uint16_t segment_start = 0;
		for (uint16_t i = 0; i < segment_count; i++) {
			uint16_t start_code = read_u16(r);
			if (i == segment_index) {
				segment_start = start_code;
				found_segment = (start_code <= codepoint);
			}
		}

		int16_t segment_delta = 0;
		for (uint16_t i = 0; i < segment_count; i++) {
			int16_t id_delta = (int16_t)read_u16(r);
			if (i == segment_index) {
				segment_delta = id_delta;
			}
		}

		uint16_t segment_range_offset = 0;
		size_t address = 0;
		for (uint16_t i = 0; i < segment_count; i++) {
			uint16_t id_range_offset = read_u16(r);
			if (i == segment_index) {
				segment_range_offset = id_range_offset;
				address = r->pos;
			}
		}

		if (found_segment) {
			if (segment_range_offset == 0) {
				result = (codepoint + segment_delta) & 0xFFFF;
			} else {
				r->pos = address + segment_range_offset + 2 * (codepoint - segment_start);
				result = read_u16(r);
			}
		}

		r->pos = orig_pos;
	}

	return result;
}

static uint32_t get_glyph_offset(reader *r, uint32_t glyph_index, int loca_format)
{
	uint32_t offset = 0;

	if (loca_format == 0) {
		r->pos += sizeof(uint16_t) * glyph_index;
		uint16_t offset_divided_by_two = read_u16(r);
		offset = offset_divided_by_two * 2;
	} else {
		r->pos += sizeof(uint32_t) * glyph_index;
		offset = read_u32(r);
	}

	return offset;
}

static glyph convert_simple_glyph(reader *r)
{
	glyph result = {0};
	int16_t contour_count = read_u16(r);
	result.x_min = read_u16(r);
	result.y_min = read_u16(r);
	result.x_max = read_u16(r);
	result.y_max = read_u16(r);

	assert(contour_count > 0);

	uint16_t *end_points = calloc(contour_count, sizeof(*end_points));
	for (uint16_t i = 0; i < contour_count; i++) {
		end_points[i] = read_u16(r);
	}

	uint16_t point_count = end_points[contour_count - 1] + 1;

	// Ignore any instructions
	uint16_t instruction_length = read_u16(r);
	r->pos += instruction_length;

	// Decode the flags
	uint8_t repeat_count = 0;
	uint8_t *flags = calloc(point_count, sizeof(*flags));
	for (uint16_t i = 0; i < point_count; i++) {
		if (repeat_count > 0) {
			flags[i] = flags[i - 1];
			repeat_count -= 1;
		} else {
			flags[i] = read_u8(r);
			if (flags[i] & OTF_GLYF_REPEAT_FLAG) {
				repeat_count = read_u8(r);
			}
		}
	}

	point *points = calloc(point_count, sizeof(*points));

	// Decode x-coordinates
	int16_t x = 0;
	for (uint16_t i = 0; i < point_count; i++) {
		uint8_t flag = flags[i];
		if (flag & OTF_GLYF_X_SHORT_VECTOR) {
			uint8_t dx = read_u8(r);
			if (flag & OTF_GLYF_X_IS_SAME) {
				x += dx;
			} else {
				x -= dx;
			}
		} else if (!(flag & OTF_GLYF_X_IS_SAME)) {
			x += (int16_t)read_u16(r);
		}

		points[i].x = x;
	}

	// Decode y-coordinates
	int16_t y = 0;
	for (uint16_t i = 0; i < point_count; i++) {
		uint8_t flag = flags[i];

		if (flag & OTF_GLYF_Y_SHORT_VECTOR) {
			uint8_t dy = read_u8(r);

			if (flag & OTF_GLYF_Y_IS_SAME) {
				y += dy;
			} else {
				y -= dy;
			}
		} else if (!(flag & OTF_GLYF_Y_IS_SAME)) {
			y += (int16_t)read_u16(r);
		}

		points[i].y = y;
	}

	// Insert midpoints
	result.points = calloc(2 * point_count, sizeof(*result.points));
	result.contours = calloc(contour_count, sizeof(*result.contours));
	result.contour_count = contour_count;

	uint32_t contour_start = 0;
	uint32_t out = 0;
	for (uint16_t j = 0; j < contour_count; j++) {
		uint16_t contour_end = end_points[j];
		point prev_point = points[contour_end];
		uint8_t prev_flag = flags[contour_end];
		for (uint16_t i = contour_start; i <= contour_end; i++) {
			point curr_point = points[i];
			uint8_t curr_flag = flags[i];
			if ((curr_flag & OTF_GLYF_ON_CURVE_POINT) == (prev_flag & OTF_GLYF_ON_CURVE_POINT)) {
				point midpoint = {0};
				midpoint.x = (prev_point.x + curr_point.x) / 2;
				midpoint.y = (prev_point.y + curr_point.y) / 2;

				result.points[out++] = midpoint;
			}

			result.points[out++] = curr_point;
			prev_point = curr_point;
			prev_flag = curr_flag;
		}

		contour_end = contour_start + 1;
		result.contours[j] = out;
	}

	result.points = realloc(result.points, out * sizeof(*result.points));
	return result;
}

static void convert_glyph(reader *r)
{
	int16_t contour_count = read_u16(r);
	if (contour_count >= 0) {
		r->pos = 0;
		convert_simple_glyph(r);
	} else {
		int16_t _x_min = read_u16(r);
		int16_t _y_min = read_u16(r);
		int16_t _x_max = read_u16(r);
		int16_t _y_max = read_u16(r);

		assert(!"TODO");
	}
}

/* Returns the format for the `loca` table. The format is 0 for 16-bit offsets
 * and 1 for 32-bit offsets. 16-bit offsets must be multiplied by two. */
static otf_head read_head(reader *r)
{
	otf_head result = {0};
	uint16_t _major_version = read_u16(r);
	uint16_t _minor_version = read_u16(r);
	uint32_t _font_revision = read_u32(r);
	uint32_t _checksum_adjustment = read_u32(r);
	uint32_t _magic_number = read_u32(r);
	uint16_t _flags = read_u16(r);
	result.units_per_em = read_u16(r);
	int64_t _created = read_u64(r);
	int64_t _modified = read_u64(r);
	int16_t _x_min = read_u16(r);
	int16_t _y_min = read_u16(r);
	int16_t _x_max = read_u16(r);
	int16_t _y_max = read_u16(r);
	uint16_t _mac_style = read_u16(r);
	uint16_t _lowest_rec_ppem = read_u16(r);
	int16_t _font_direction_hint = read_u16(r);
	result.index_to_loc_format = read_u16(r);
	int16_t _glyph_data_format = read_u16(r);

	return result;
}

static uint16_t get_glyph_count(reader *r)
{
	uint32_t _version = read_u32(r);
	uint32_t num_glyphs = read_u16(r);
	return num_glyphs;
}

int main(void)
{
	reader r = {0};
	r.input = read_file("fonts/OpenSans-Regular.ttf");
	if (!r.input.at) {
		return -1;
	}

	char c = 'A';
	glyph glyph = {0};
	otf_head font = {0};
	{
		uint32_t _version = read_u32(&r);
		uint16_t num_tables = read_u16(&r);
		uint16_t _search_range = read_u16(&r);
		uint16_t _entry_selector = read_u16(&r);
		uint16_t _range_shift = read_u16(&r);

		str tables[OTF_TABLE_COUNT] = {0};
		for (uint16_t i = 0; i < num_tables; i++) {
			char *tag_str = r.input.at + r.pos;
			uint32_t tag = read_u32(&r);
			uint32_t _checksum = read_u32(&r);
			uint32_t offset = read_u32(&r);
			uint32_t length = read_u32(&r);

			switch (tag) {
			case TAG('c', 'm', 'a', 'p'):
				tag = OTF_TABLE_CMAP;
				break;
			case TAG('g', 'l', 'y', 'f'):
				tag = OTF_TABLE_GLYF;
				break;
			case TAG('h', 'e', 'a', 'd'):
				tag = OTF_TABLE_HEAD;
				break;
			case TAG('l', 'o', 'c', 'a'):
				tag = OTF_TABLE_LOCA;
				break;
			case TAG('m', 'a', 'x', 'p'):
				tag = OTF_TABLE_MAXP;
				break;
			default:
				tag = OTF_TABLE_NONE;
			}

			if (tag != 0) {
				tables[tag] = r.input;
				tables[tag].at += offset;
				tables[tag].length = length;
			}
		}

		reader cmap = {tables[OTF_TABLE_CMAP]};
		reader loca = {tables[OTF_TABLE_LOCA]};
		reader glyf = {tables[OTF_TABLE_GLYF]};
		reader head = {tables[OTF_TABLE_HEAD]};
		reader maxp = {tables[OTF_TABLE_MAXP]};

		uint32_t index = get_glyph_index(&cmap, 'A');
		font = read_head(&head);
		uint32_t offset = get_glyph_offset(&loca, index, font.index_to_loc_format);

		glyf.input.at += offset;
		glyf.input.length -= offset;

		glyph = convert_simple_glyph(&glyf);
	}

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

	const char *vertex_shader_source =
		"#version 410 core\n"
		"uniform float x_min;\n"
		"uniform float y_min;\n"
		"uniform float x_max;\n"
		"uniform float y_max;\n"
		"void main()\n"
		"{\n"
		"    vec4 vertices[4];\n"
		"    vertices[0] = vec4(x_min, y_min, 0.0, 1.0);\n"
		"    vertices[1] = vec4(x_max, y_min, 0.0, 1.0);\n"
		"    vertices[2] = vec4(x_min, y_max, 0.0, 1.0);\n"
		"    vertices[3] = vec4(x_max, y_max, 0.0, 1.0);\n"
		"\n"
		"    gl_Position = vertices[gl_VertexID];\n"
		"}\n";
	GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
	glCompileShader(vertex_shader);

	const char *fragment_shader_source =
		"#version 410 core\n"
		"uniform sampler2D point_data;\n"
		"uniform sampler2D contour_data;\n"
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

	float width = (float)(glyph.x_max - glyph.x_min) / font.units_per_em;
	float height = (float)(glyph.y_max - glyph.y_min) / font.units_per_em;

	glUseProgram(program);
	glUniform1f(glGetUniformLocation(program, "x_min"), 0);
	glUniform1f(glGetUniformLocation(program, "y_min"), 0);
	glUniform1f(glGetUniformLocation(program, "x_max"), width);
	glUniform1f(glGetUniformLocation(program, "y_max"), height);

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
