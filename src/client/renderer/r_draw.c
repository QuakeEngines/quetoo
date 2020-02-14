/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "r_local.h"
#include "client.h"

typedef struct {
	char name[MAX_QPATH];

	r_image_t *image;

	r_pixel_t char_width;
	r_pixel_t char_height;
} r_font_t;

#define MAX_DRAW_FONTS 3

typedef struct {
	GLenum mode;
	GLuint texture;

	GLint first_vertex;
	GLsizei num_vertexes;
} r_draw_arrays_t;

#define MAX_DRAW_GEOMETRY 4096

typedef struct {
	vec2s_t position;
	vec2_t diffuse;
	color_t color;
} r_draw_vertex_t;

#define MAX_DRAW_VERTEXES (MAX_DRAW_GEOMETRY * 6)

static struct {

	// registered fonts
	int32_t num_fonts;
	r_font_t fonts[MAX_DRAW_FONTS];

	// active font
	r_font_t *font;

	// accumulated draw arrays to draw for this frame
	r_draw_arrays_t draw_arrays[MAX_DRAW_GEOMETRY];
	int32_t num_draw_arrays;

	// accumulated vertexes backing the draw arrays
	r_draw_vertex_t vertexes[MAX_DRAW_VERTEXES];
	int32_t num_vertexes;

	// the vertex array
	GLuint vertex_array;

	// the vertex buffer
	GLuint vertex_buffer;

} r_draw;

/**
 * @brief The draw program.
 */
static struct {
	GLuint name;

	GLint in_position;
	GLint in_diffuse;
	GLint in_color;

	GLint projection;

	GLint texture_diffuse;

	GLint brightness;
	GLint contrast;
	GLint saturation;
	GLint gamma;
} r_draw_program;

/**
 * @brief
 */
static void R_AddDrawArrays(const r_draw_arrays_t *draw) {

	if (r_draw.num_draw_arrays == MAX_DRAW_GEOMETRY) {
		Com_Warn("MAX_DRAW_GEOMETRY\n");
		return;
	}

	if (draw->num_vertexes == 0) {
		return;
	}

	r_draw.draw_arrays[r_draw.num_draw_arrays] = *draw;
	r_draw.num_draw_arrays++;

	r_view.count_draw_arrays++;
}

/**
 * @brief Emits GL_TRIANGLES data from the specified quad.
 */
static void R_EmitDrawVertexes_Quad(const r_draw_vertex_t *quad) {

	if (r_draw.num_vertexes + 6 > MAX_DRAW_VERTEXES) {
		Com_Warn("MAX_DRAW_VERTEXES\n");
		return;
	}

	r_draw.vertexes[r_draw.num_vertexes++] = quad[0];
	r_draw.vertexes[r_draw.num_vertexes++] = quad[1];
	r_draw.vertexes[r_draw.num_vertexes++] = quad[2];

	r_draw.vertexes[r_draw.num_vertexes++] = quad[0];
	r_draw.vertexes[r_draw.num_vertexes++] = quad[2];
	r_draw.vertexes[r_draw.num_vertexes++] = quad[3];

	r_view.count_draw_quads++;
}

/**
 * @brief
 */
static void R_DrawChar_(r_pixel_t x, r_pixel_t y, char c, const color_t color) {

	if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
		return;
	}

	const uint32_t row = (uint32_t) c >> 4;
	const uint32_t col = (uint32_t) c & 15;

	const float s0 = col * 0.0625;
	const float t0 = row * 0.1250;
	const float s1 = (col + 1) * 0.0625;
	const float t1 = (row + 1) * 0.1250;

	r_draw_vertex_t quad[4];

	quad[0].position = Vec2s(x, y);
	quad[1].position = Vec2s(x + r_draw.font->char_width, y);
	quad[2].position = Vec2s(x + r_draw.font->char_width, y + r_draw.font->char_height);
	quad[3].position = Vec2s(x, y + r_draw.font->char_height);

	quad[0].diffuse = Vec2(s0, t0);
	quad[1].diffuse = Vec2(s1, t0);
	quad[2].diffuse = Vec2(s1, t1);
	quad[3].diffuse = Vec2(s0, t1);

	quad[0].color = color;
	quad[1].color = color;
	quad[2].color = color;
	quad[3].color = color;

	R_EmitDrawVertexes_Quad(quad);
}

/**
 * @brief
 */
void R_DrawChar(r_pixel_t x, r_pixel_t y, char c, const color_t color) {

	r_draw_arrays_t draw = {
		.mode = GL_TRIANGLES,
		.texture = r_draw.font->image->texnum,
		.first_vertex = r_draw.num_vertexes,
		.num_vertexes = 6
	};

	R_DrawChar_(x, y, c, color);
	R_AddDrawArrays(&draw);
}

/**
 * @brief Return the width of the specified string in pixels. This will vary based
 * on the currently bound font. Color escapes are omitted.
 */
r_pixel_t R_StringWidth(const char *s) {
	return StrColorLen(s) * r_draw.font->char_width;
}

/**
 * @brief
 */
size_t R_DrawString(r_pixel_t x, r_pixel_t y, const char *s, const color_t color) {
	return R_DrawSizedString(x, y, s, UINT16_MAX, UINT16_MAX, color);
}

/**
 * @brief
 */
size_t R_DrawBytes(r_pixel_t x, r_pixel_t y, const char *s, size_t size, const color_t color) {
	return R_DrawSizedString(x, y, s, size, size, color);
}

/**
 * @brief Draws at most len chars or size bytes of the specified string. Color escape
 * sequences are not visible chars. Returns the number of chars drawn.
 */
size_t R_DrawSizedString(r_pixel_t x, r_pixel_t y, const char *s, size_t len, size_t size, const color_t color) {
	size_t i, j;

	r_draw_arrays_t draw = {
		.mode = GL_TRIANGLES,
		.texture = r_draw.font->image->texnum,
		.first_vertex = r_draw.num_vertexes
	};

	color_t c = color;

	i = j = 0;
	while (*s && i < len && j < size) {

		if (StrIsColor(s)) {
			c = ColorEsc(StrColor(s));
			j += 2;
			s += 2;
			continue;
		}

		R_DrawChar_(x, y, *s, c);
		x += r_draw.font->char_width; // next char position in line

		i++;
		j++;
		s++;
	}

	draw.num_vertexes = r_draw.num_vertexes - draw.first_vertex;
	R_AddDrawArrays(&draw);

	return i;
}


/**
 * @brief Binds the specified font, returning the character width and height.
 */
void R_BindFont(const char *name, r_pixel_t *cw, r_pixel_t *ch) {

	if (name == NULL) {
		name = "medium";
	}

	int32_t i;
	for (i = 0; i < r_draw.num_fonts; i++) {
		if (!g_strcmp0(name, r_draw.fonts[i].name)) {
			if (r_context.high_dpi && i < r_draw.num_fonts - 1) {
				r_draw.font = &r_draw.fonts[i + 1];
			} else {
				r_draw.font = &r_draw.fonts[i];
			}
			break;
		}
	}

	if (i == r_draw.num_fonts) {
		Com_Warn("Unknown font: %s\n", name);
	}

	if (cw) {
		*cw = r_draw.font->char_width;
	}

	if (ch) {
		*ch = r_draw.font->char_height;
	}
}

/**
 * @brief
 */
void R_DrawImageRect(r_pixel_t x, r_pixel_t y, r_pixel_t w, r_pixel_t h, const r_image_t *image, const color_t color) {

	r_draw_arrays_t draw = {
		.mode = GL_TRIANGLES,
		.texture = image->texnum,
		.first_vertex = r_draw.num_vertexes,
		.num_vertexes = 6
	};

	r_draw_vertex_t quad[4];

	quad[0].position = Vec2s(x, y);
	quad[1].position = Vec2s(x + w, y);
	quad[2].position = Vec2s(x + w, y + h);
	quad[3].position = Vec2s(x, y + h);

	quad[0].diffuse = Vec2(0, 0);
	quad[1].diffuse = Vec2(1, 0);
	quad[2].diffuse = Vec2(1, 1);
	quad[3].diffuse = Vec2(0, 1);

	quad[0].color = color;
	quad[1].color = color;
	quad[2].color = color;
	quad[3].color = color;

	R_EmitDrawVertexes_Quad(quad);
	R_AddDrawArrays(&draw);
}

/**
 * @brief
 */
void R_DrawImage(r_pixel_t x, r_pixel_t y, float scale, const r_image_t *image, const color_t color) {

	R_DrawImageRect(x, y, image->width * scale, image->height * scale, image, color);
}

/**
 * @brief The color can be specified as an index into the palette with positive alpha
 * value for a, or as an RGBA value (32 bit) by passing -1.0 for a.
 */
void R_DrawFill(r_pixel_t x, r_pixel_t y, r_pixel_t w, r_pixel_t h, const color_t color) {

	r_draw_arrays_t draw = {
		.mode = GL_TRIANGLES,
		.texture = r_image_state.null->texnum,
		.first_vertex = r_draw.num_vertexes,
		.num_vertexes = 6
	};

	r_draw_vertex_t quad[4];

	quad[0].position = Vec2s(x, y);
	quad[1].position = Vec2s(x + w, y);
	quad[2].position = Vec2s(x + w, y + h);
	quad[3].position = Vec2s(x, y + h);

	quad[0].color = color;
	quad[1].color = color;
	quad[2].color = color;
	quad[3].color = color;

	R_EmitDrawVertexes_Quad(quad);
	R_AddDrawArrays(&draw);
}

/**
 * @brief
 */
void R_DrawLines(const r_pixel_t *points, size_t count, const color_t color) {

	r_draw_arrays_t draw = {
		.mode = GL_LINE_STRIP,
		.texture = r_image_state.null->texnum,
		.first_vertex = r_draw.num_vertexes,
		.num_vertexes = (GLsizei) count
	};

	r_draw_vertex_t *out = r_draw.vertexes + r_draw.num_vertexes;

	const r_pixel_t *in = points;
	for (size_t i = 0; i < count; i++, in += 2, out++) {

		out->position.x = *(in + 0);
		out->position.y = *(in + 1);

		out->color = color;
	}

	r_draw.num_vertexes += count;

	R_AddDrawArrays(&draw);
}

/**
 * @brief Draw all 2D geometry accumulated for the current frame.
 */
void R_Draw2D(void) {

	glViewport(0, 0, r_context.width, r_context.height);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(r_draw_program.name);

	Matrix4x4_FromOrtho(&r_locals.projection2D, 0.0, r_context.width, r_context.height, 0.0, -1.0, 1.0);
	glUniformMatrix4fv(r_draw_program.projection, 1, GL_FALSE, (GLfloat *) r_locals.projection2D.m);

	glUniform1f(r_draw_program.brightness, r_brightness->value);
	glUniform1f(r_draw_program.contrast, r_contrast->value);
	glUniform1f(r_draw_program.saturation, r_saturation->value);
	glUniform1f(r_draw_program.gamma, r_gamma->value);

	glBindVertexArray(r_draw.vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, r_draw.vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, r_draw.num_vertexes * sizeof(r_draw_vertex_t), r_draw.vertexes, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(r_draw_program.in_position);
	glEnableVertexAttribArray(r_draw_program.in_diffuse);
	glEnableVertexAttribArray(r_draw_program.in_color);

	const r_draw_arrays_t *draw = r_draw.draw_arrays;
	for (int32_t i = 0; i < r_draw.num_draw_arrays; i++, draw++) {

		glBindTexture(GL_TEXTURE_2D, draw->texture);

		glDrawArrays(draw->mode, draw->first_vertex, draw->num_vertexes);
	}

	glUseProgram(0);

	r_draw.num_draw_arrays = 0;
	r_draw.num_vertexes = 0;

	glBlendFunc(GL_ONE, GL_ZERO);
	glDisable(GL_BLEND);

	R_GetError(NULL);
}

/**
 * @brief Initializes the specified bitmap font. The layout of the font is square,
 * 2^n (e.g. 256x256, 512x512), and 8 rows by 16 columns. See below:
 *
 *
 *  !"#$%&'()*+,-./
 * 0123456789:;<=>?
 * @ABCDEFGHIJKLMNO
 * PQRSTUVWXYZ[\]^_
 * 'abcdefghijklmno
 * pqrstuvwxyz{|}"
 */
static void R_InitFont(char *name) {

	if (r_draw.num_fonts == MAX_DRAW_FONTS) {
		Com_Error(ERROR_DROP, "MAX_DRAW_FONTS\n");
		return;
	}

	r_font_t *font = &r_draw.fonts[r_draw.num_fonts++];

	g_strlcpy(font->name, name, sizeof(font->name));

	font->image = R_LoadImage(va("fonts/%s", name), IT_FONT);

	font->char_width = font->image->width / 16;
	font->char_height = font->image->height / 8;

	Com_Debug(DEBUG_RENDERER, "%s (%dx%d)\n", font->name, font->char_width, font->char_height);
}

/**
 * @brief
 */
static void R_InitDrawProgram(void) {

	memset(&r_draw_program, 0, sizeof(r_draw_program));

	r_draw_program.name = R_LoadProgram(
			&MakeShaderDescriptor(GL_VERTEX_SHADER, "draw_vs.glsl"),
			&MakeShaderDescriptor(GL_FRAGMENT_SHADER, "color_filter.glsl", "draw_fs.glsl"),
			NULL);

	glUseProgram(r_draw_program.name);

	r_draw_program.in_position = glGetAttribLocation(r_draw_program.name, "in_position");
	r_draw_program.in_diffuse = glGetAttribLocation(r_draw_program.name, "in_diffuse");
	r_draw_program.in_color = glGetAttribLocation(r_draw_program.name, "in_color");

	r_draw_program.projection = glGetUniformLocation(r_draw_program.name, "projection");

	r_draw_program.texture_diffuse = glGetUniformLocation(r_draw_program.name, "texture_diffuse");

	r_draw_program.brightness = glGetUniformLocation(r_draw_program.name, "brightness");
	r_draw_program.contrast = glGetUniformLocation(r_draw_program.name, "contrast");
	r_draw_program.saturation = glGetUniformLocation(r_draw_program.name, "saturation");
	r_draw_program.gamma = glGetUniformLocation(r_draw_program.name, "gamma");

	glUniform1i(r_draw_program.texture_diffuse, 0);

	R_GetError(NULL);
}

/**
 * @brief
 */
void R_InitDraw(void) {

	memset(&r_draw, 0, sizeof(r_draw));

	R_InitFont("small");
	R_InitFont("medium");
	R_InitFont("large");

	R_BindFont(NULL, NULL, NULL);

	glGenVertexArrays(1, &r_draw.vertex_array);
	glBindVertexArray(r_draw.vertex_array);

	glGenBuffers(1, &r_draw.vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, r_draw.vertex_buffer);

	glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, sizeof(r_draw_vertex_t), (void *) offsetof(r_draw_vertex_t, position));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(r_draw_vertex_t), (void *) offsetof(r_draw_vertex_t, diffuse));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(r_draw_vertex_t), (void *) offsetof(r_draw_vertex_t, color));

	glBindVertexArray(0);

	R_GetError(NULL);

	R_InitDrawProgram();
}

/**
 * @brief
 */
static void R_ShutdownDrawProgram(void) {

	glDeleteProgram(r_draw_program.name);

	r_draw_program.name = 0;
}

/**
 * @brief
 */
void R_ShutdownDraw(void) {

	glDeleteVertexArrays(1, &r_draw.vertex_array);
	glDeleteBuffers(1, &r_draw.vertex_buffer);

	R_ShutdownDrawProgram();
}
