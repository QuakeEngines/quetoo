/*
 * Copyright(c) 1997-2001 Id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quake2World.
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

/*
 * Static light source loading.
 *
 * Static light sources provide directional diffuse lighting for mesh models.
 * There are several radius scale factors for the types of light sources we
 * will encounter. The goal is to normalize all light source intensities to
 * that of standard "point" lights (light entities in the map editor).
 *
 * Surface light source radius is dependent upon surface area. For the vast
 * majority of surface lights, this means an aggressive up-scale yields the
 * best results.
 *
 * Sky surfaces are also considered light sources when sun lighting was enabled
 * during the light compilation of the level. Given that sky surface area is
 * typically quite large, their surface area coefficient actually scales down.
 */

#define BSP_LIGHT_SUN_SCALE 2.0
#define BSP_LIGHT_MERGE_THRESHOLD 32.0
#define BSP_LIGHT_SURFACE_RADIUS_SCALE 1.0
#define BSP_LIGHT_POINT_RADIUS_SCALE 1.0
#define BSP_LIGHT_COLOR_COMPONENT_MAX 0.9

r_bsp_light_state_t r_bsp_light_state;

/*
 * @brief Resolves ambient light, brightness, and contrast levels from Worldspawn.
 */
static void R_ResolveBspLightParameters(void) {
	const char *c;
	vec3_t tmp;

	// resolve ambient light
	if ((c = Cm_WorldspawnValue("ambient"))) {
		vec_t *f = r_bsp_light_state.ambient;
		sscanf(c, "%f %f %f", &f[0], &f[1], &f[2]);

		VectorScale(f, r_modulate->value, f);

		Com_Debug("Resolved ambient: %s\n", vtos(f));
	} else {
		VectorSet(r_bsp_light_state.ambient, 0.15, 0.15, 0.15);
	}

	// resolve sun light
	if ((c = Cm_WorldspawnValue("sun_light"))) {
		r_bsp_light_state.sun.diffuse = atof(c) * BSP_LIGHT_SUN_SCALE;

		Com_Debug("Resolved sun_light: %1.2f\n", r_bsp_light_state.sun.diffuse);
	} else {
		r_bsp_light_state.sun.diffuse = 0.0;
	}

	// resolve sun angles and direction
	if ((c = Cm_WorldspawnValue("sun_angles"))) {
		VectorClear(tmp);
		sscanf(c, "%f %f", &tmp[0], &tmp[1]);

		Com_Debug("Resolved sun_angles: %s\n", vtos(tmp));
		AngleVectors(tmp, r_bsp_light_state.sun.dir, NULL, NULL);
	} else {
		VectorCopy(vec3_down, r_bsp_light_state.sun.dir);
	}

	// resolve sun color
	if ((c = Cm_WorldspawnValue("sun_color"))) {
		vec_t *f = r_bsp_light_state.sun.color;
		sscanf(c, "%f %f %f", &f[0], &f[1], &f[2]);

		Com_Debug("Resolved sun_color: %s\n", vtos(f));
	} else {
		VectorSet(r_bsp_light_state.sun.color, 1.0, 1.0, 1.0);
	}

	// resolve brightness
	if ((c = Cm_WorldspawnValue("brightness"))) {
		r_bsp_light_state.brightness = atof(c);
	} else {
		r_bsp_light_state.brightness = 1.0;
	}

	// resolve saturation
	if ((c = Cm_WorldspawnValue("saturation"))) {
		r_bsp_light_state.saturation = atof(c);
	} else {
		r_bsp_light_state.saturation = 1.0;
	}

	// resolve contrast
	if ((c = Cm_WorldspawnValue("contrast"))) {
		r_bsp_light_state.contrast = atof(c);
	} else {
		r_bsp_light_state.contrast = 1.0;
	}

	Com_Debug("Resolved brightness: %1.2f\n", r_bsp_light_state.brightness);
	Com_Debug("Resolved saturation: %1.2f\n", r_bsp_light_state.saturation);
	Com_Debug("Resolved contrast: %1.2f\n", r_bsp_light_state.contrast);

	const vec_t brt = r_bsp_light_state.brightness;
	const vec_t sat = r_bsp_light_state.saturation;
	const vec_t con = r_bsp_light_state.contrast;

	// apply brightness, saturation and contrast to the colors
	ColorFilter(r_bsp_light_state.ambient, r_bsp_light_state.ambient, brt, sat, con);
	Com_Debug("Scaled ambient: %s\n", vtos(r_bsp_light_state.ambient));

	ColorFilter(r_bsp_light_state.sun.color, r_bsp_light_state.sun.color, brt, sat, con);
	Com_Debug("Scaled sun color: %s\n", vtos(r_bsp_light_state.sun.color));
}

/*
 * @brief Adds the specified static light source after first ensuring that it
 * can not be merged with any known sources.
 */
static void R_AddBspLight(r_bsp_model_t *bsp, vec3_t org, vec_t radius, vec3_t color) {

	if (radius <= 0.0) {
		Com_Debug("Bad radius: %f\n", radius);
		return;
	}

	r_bsp_light_t *l = NULL;
	GList *e = r_bsp_light_state.lights;
	while (e) {
		vec3_t delta;

		l = (r_bsp_light_t *) e->data;
		VectorSubtract(org, l->origin, delta);

		if (VectorLength(delta) <= BSP_LIGHT_MERGE_THRESHOLD) // merge them
			break;

		l = NULL;
		e = e->next;
	}

	if (!l) { // or allocate a new one
		l = Mem_LinkMalloc(sizeof(*l), bsp);
		r_bsp_light_state.lights = g_list_prepend(r_bsp_light_state.lights, l);

		VectorCopy(org, l->origin);
		l->leaf = R_LeafForPoint(l->origin, bsp);
	}

	l->count++;
	l->radius = ((l->radius * (l->count - 1)) + radius) / l->count;

	VectorMix(l->color, color, 1.0 / l->count, l->color);
}

/*
 * @brief Parse the entity string and resolve all static light sources. Sources which
 * are very close to each other are merged. Their colors are blended according
 * to their light value (intensity).
 */
void R_LoadBspLights(r_bsp_model_t *bsp) {
	vec3_t org, color;
	vec_t radius;
	int32_t i;

	memset(&r_bsp_light_state, 0, sizeof(r_bsp_light_state));

	R_ResolveBspLightParameters();

	// iterate the world surfaces for surface lights
	const r_bsp_surface_t *surf = bsp->surfaces;

	for (i = 0; i < bsp->num_surfaces; i++, surf++) {

		// light-emitting surfaces are of course lights
		if ((surf->texinfo->flags & SURF_LIGHT) && surf->texinfo->value) {

			VectorMA(surf->center, 1.0, surf->normal, org);

			const vec_t x = Clamp(surf->maxs[0] - surf->mins[0], 1.0, HUGE_VALF);
			const vec_t y = Clamp(surf->maxs[1] - surf->mins[1], 1.0, HUGE_VALF);
			const vec_t z = Clamp(surf->maxs[2] - surf->mins[2], 1.0, HUGE_VALF);

			radius = sqrt(x * y * z) * BSP_LIGHT_SURFACE_RADIUS_SCALE;

			R_AddBspLight(bsp, org, radius, surf->texinfo->material->diffuse->color);
		}
	}

	// parse the entity string for point lights
	const char *ents = Cm_EntityString();

	VectorClear(org);

	radius = 300.0;
	VectorSet(color, 1.0, 1.0, 1.0);

	char class_name[MAX_QPATH];
	_Bool entity = false, light = false;

	while (true) {

		const char *c = ParseToken(&ents);

		if (!strlen(c))
			break;

		if (*c == '{')
			entity = true;

		if (!entity) // skip any whitespace between ents
			continue;

		if (*c == '}') {
			entity = false;

			if (light) { // add it

				R_AddBspLight(bsp, org, radius * BSP_LIGHT_POINT_RADIUS_SCALE, color);

				radius = 300.0;
				VectorSet(color, 1.0, 1.0, 1.0);

				light = false;
			}
		}

		if (!g_strcmp0(c, "classname")) {

			c = ParseToken(&ents);
			g_strlcpy(class_name, c, sizeof(class_name));

			if (!strncmp(c, "light", 5)) // light, light_spot, etc..
				light = true;

			continue;
		}

		if (!g_strcmp0(c, "origin")) {
			sscanf(ParseToken(&ents), "%f %f %f", &org[0], &org[1], &org[2]);
			continue;
		}

		if (!g_strcmp0(c, "light")) {
			radius = atof(ParseToken(&ents));
			continue;
		}

		if (!g_strcmp0(c, "_color")) {
			sscanf(ParseToken(&ents), "%f %f %f", &color[0], &color[1], &color[2]);
			continue;
		}
	}

	// allocate the lights array
	bsp->num_bsp_lights = g_list_length(r_bsp_light_state.lights);
	bsp->bsp_lights = Mem_LinkMalloc(sizeof(r_bsp_light_t) * bsp->num_bsp_lights, bsp);

	i = 0;
	GList *e = r_bsp_light_state.lights;
	while (e) {
		r_bsp_light_t *l = (r_bsp_light_t *) e->data;

		// normalize the color, scaling back from full brights
		vec_t max = 0.0;
		int32_t j;

		for (j = 0; j < 3; j++) {
			if (l->color[j] > max)
				max = l->color[j];
		}

		if (max > BSP_LIGHT_COLOR_COMPONENT_MAX) {
			VectorScale(l->color, BSP_LIGHT_COLOR_COMPONENT_MAX / max, l->color);
		}

		// and lastly copy the light into the BSP model
		bsp->bsp_lights[i++] = *l;
		e = e->next;
	}

	// reset state
	g_list_free_full(r_bsp_light_state.lights, Mem_Free);
	r_bsp_light_state.lights = NULL;

	Com_Debug("Loaded %d bsp lights\n", bsp->num_bsp_lights);
}
