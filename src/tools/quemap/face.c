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

#include "bsp.h"
#include "face.h"
#include "map.h"
#include "material.h"

int32_t c_merge;
static int32_t c_faces;

/**
 * @brief
 */
face_t *AllocFace(void) {

	face_t *f = Mem_TagMalloc(sizeof(*f), MEM_TAG_FACE);
	c_faces++;

	f->num = -1;

	return f;
}

/**
 * @brief
 */
static face_t *NewFaceFromFace(const face_t *f) {

	face_t *newf = AllocFace();
	*newf = *f;
	newf->merged = NULL;
	newf->w = NULL;
	return newf;
}

/**
 * @brief
 */
void FreeFace(face_t *f) {

	if (f->w) {
		Cm_FreeWinding(f->w);
	}

	Mem_Free(f);
	c_faces--;
}


/**
 * @brief If two polygons share a common edge and the edges that meet at the
 * common points are both inside the other polygons, merge them
 *
 * @return NULL if the faces couldn't be merged, or the new face.
 * @remark The originals will NOT be freed.
 */
face_t *MergeFaces(face_t *f1, face_t *f2, const vec3_t normal) {

	if (!f1->w || !f2->w) {
		return NULL;
	}
	if (f1->texinfo != f2->texinfo) {
		return NULL;
	}
	if (f1->plane_num != f2->plane_num) { // on front and back sides
		return NULL;
	}
	if (f1->contents != f2->contents) {
		return NULL;
	}

	cm_winding_t *nw = Cm_MergeWindings(f1->w, f2->w, normal);
	if (!nw) {
		return NULL;
	}

	c_merge++;
	face_t *newf = NewFaceFromFace(f1);
	newf->w = nw;

	f1->merged = newf;
	f2->merged = newf;

	return newf;
}

#define SNAP_TO_INT		(8.0)
#define SNAP_TO_FLOAT	(1.0 / SNAP_TO_INT)

/**
 * @brief Emits a vertex array for the given face.
 */
static int32_t EmitFaceVertexes(const face_t *face) {

	const bsp_texinfo_t *texinfo = &bsp_file.texinfo[face->texinfo];

	const vec_t *sdir = texinfo->vecs[0];
	const vec_t *tdir = texinfo->vecs[1];

	const SDL_Surface *diffuse = LoadDiffuseTexture(texinfo->texture);
	if (diffuse == NULL) {
		Com_Warn("Failed to load %s\n", texinfo->texture);
	}

	for (int32_t i = 0; i < face->w->num_points; i++) {

		if (bsp_file.num_vertexes == MAX_BSP_VERTEXES) {
			Com_Error(ERROR_FATAL, "MAX_BSP_VERTEXES");
		}

		bsp_vertex_t out = {
			.texinfo = face->texinfo
		};

		VectorCopy(face->w->points[i], out.position);

		if (!(texinfo->flags & SURF_NO_WELD)) {
			for (int32_t j = 0; j < 3; j++) {
				out.position[j] = SNAP_TO_FLOAT * floorf(out.position[j] * SNAP_TO_INT + 0.5);
			}
			VectorCopy(out.position, face->w->points[i]);
		}

		VectorCopy(planes[face->plane_num].normal, out.normal);
		TangentVectors(out.normal, sdir, tdir, out.tangent, out.bitangent);

		const vec_t s = DotProduct(out.position, sdir) + sdir[3];
		const vec_t t = DotProduct(out.position, tdir) + tdir[3];

		out.diffuse[0] = s / (diffuse ? diffuse->w : 1.0);
		out.diffuse[1] = t / (diffuse ? diffuse->h : 1.0);

		bsp_file.vertexes[bsp_file.num_vertexes] = out;
		bsp_file.num_vertexes++;
	}

	return face->w->num_points;
}

/**
 * @brief Emits a vertex elements array of triangles for the given face.
 */
static int32_t EmitFaceElements(const face_t *face) {

	const int32_t num_triangles = (face->w->num_points - 2);
	const int32_t num_elements = num_triangles * 3;

	int32_t elements[num_elements];
	Cm_ElementsForWinding(face->w, elements);

	const bsp_face_t *f = &bsp_file.faces[face->num];

	for (int32_t i = 0; i < num_elements; i++) {

		if (bsp_file.num_elements == MAX_BSP_ELEMENTS) {
			Com_Error(ERROR_FATAL, "MAX_BSP_ELEMENTS\n");
		}

		bsp_file.elements[bsp_file.num_elements] = f->first_vertex + elements[i];
		bsp_file.num_elements++;
	}

	return num_elements;
}

/**
 * @brief
 */
int32_t EmitFace(face_t *face) {

	assert(face->w->num_points > 2);

	if (face->merged) {
		return -1; // not a final face
	}

	// save output order so leaf can reference
	face->num = bsp_file.num_faces;

	if (bsp_file.num_faces == MAX_BSP_FACES) {
		Com_Error(ERROR_FATAL, "MAX_BSP_FACES\n");
	}

	bsp_face_t *out = &bsp_file.faces[bsp_file.num_faces];
	bsp_file.num_faces++;

	out->plane_num = face->plane_num;
	out->texinfo = face->texinfo;

	out->first_vertex = bsp_file.num_vertexes;
	out->num_vertexes = EmitFaceVertexes(face);

	out->first_element = bsp_file.num_elements;
	out->num_elements = EmitFaceElements(face);

	out->lightmap.num = -1;
	return bsp_file.num_faces - 1;
}

#define MAX_PHONG_FACES 256

/**
 * @brief Populate Phong faces with indexes of all Phong shaded faces referencing the vertex.
 * @return The number of Phong shaded bsp_face_t's referencing the vertex.
 */
static size_t PhongFacesForVertex(const bsp_vertex_t *vertex, const bsp_face_t **phong_faces) {

	size_t count = 0;

	const bsp_face_t *face = bsp_file.faces;
	for (int32_t i = 0; i < bsp_file.num_faces; i++, face++) {

		const bsp_texinfo_t *texinfo = &bsp_file.texinfo[face->texinfo];
		if (!(texinfo->flags & SURF_PHONG)) {
			continue;
		}

		const bsp_plane_t *plane = &bsp_file.planes[face->plane_num];
		if (DotProduct(vertex->normal, plane->normal) <= SIDE_EPSILON) {
			continue;
		}

		const bsp_vertex_t *v = &bsp_file.vertexes[face->first_vertex];
		for (uint16_t j = 0; j < face->num_vertexes; j++, v++) {

			vec3_t delta;
			VectorSubtract(vertex->position, v->position, delta);

			if (VectorLength(delta) <= ON_EPSILON) {
				phong_faces[count++] = face;
				break;
			}
		}

		if (count == MAX_PHONG_FACES) {
			Mon_SendPoint(MON_ERROR, vertex->position, "MAX_PHONG_FACES");
			break;
		}
	}

	return count;
}

/**
 * @brief Calculate per-vertex (instead of per-plane) normal vectors. This is done by finding all of
 * the faces which share a given vertex, and calculating a weighted average of their normals.
 */
void PhongVertexes(void) {
	const bsp_face_t *phong_faces[MAX_PHONG_FACES];

	bsp_vertex_t *v = bsp_file.vertexes;
	for (int32_t i = 0; i < bsp_file.num_vertexes; i++, v++) {

		const size_t count = PhongFacesForVertex(v, phong_faces);
		if (count) {

			VectorClear(v->normal);

			const bsp_face_t **pf = phong_faces;
			for (size_t j = 0; j < count; j++, pf++) {

				const plane_t *plane = &planes[(*pf)->plane_num];

				cm_winding_t *w = Cm_WindingForFace(&bsp_file, *pf);
				VectorMA(v->normal, Cm_WindingArea(w), plane->normal, v->normal);
				Cm_FreeWinding(w);
			}

			VectorNormalize(v->normal);

			const bsp_texinfo_t *texinfo = &bsp_file.texinfo[v->texinfo];

			const vec_t *sdir = texinfo->vecs[0];
			const vec_t *tdir = texinfo->vecs[1];

			TangentVectors(v->normal, sdir, tdir, v->tangent, v->bitangent);
		}
	}
}
