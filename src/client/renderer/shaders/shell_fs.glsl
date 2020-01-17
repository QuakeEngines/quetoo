/**
 * @brief Shell fragment shader.
 */

#version 330

#define FRAGMENT_SHADER

#include "include/gamma.glsl"

uniform sampler2D SAMPLER0;
uniform vec4 COLOR;

in VertexData {
	vec4 color;
	vec2 texcoord;
};

out vec4 fragColor;

/**
 * @brief Shader entry point.
 */
void main(void) {

	fragColor = COLOR * texture(SAMPLER0, texcoord);

	GammaFragment(fragColor);
}
