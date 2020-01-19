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
#include "map.h"
#include "material.h"
#include "portal.h"
#include "qbsp.h"

static int32_t c_nofaces;
static int32_t c_facenodes;

/**
 * @brief
 */
static void EmitPlanes(void) {

	const plane_t *p = planes;
	for (int32_t i = 0; i < num_planes; i++, p++) {
		bsp_plane_t *bp = &bsp_file.planes[bsp_file.num_planes];

		VectorCopy(p->normal, bp->normal);
		bp->dist = p->dist;

		bsp_file.num_planes++;
	}
}

/**
 * @brief qsort comparator to sort leaf faces by texinfo and lightmap.
 */
static int32_t EmitDrawElements_compare(const void *a, const void *b) {

	const bsp_face_t *a_face = bsp_file.faces + *(int32_t *) a;
	const bsp_face_t *b_face = bsp_file.faces + *(int32_t *) b;

	const bsp_texinfo_t *a_tex = bsp_file.texinfo + a_face->texinfo;
	const bsp_texinfo_t *b_tex = bsp_file.texinfo + b_face->texinfo;

	int32_t order = strcmp(a_tex->texture, b_tex->texture);
	if (order == 0) {
		order = a_tex->flags - b_tex->flags;
		if (order == 0) {
			order = a_face->lightmap.num - b_face->lightmap.num;
		}
	}
	return order;
}

/**
 * @brief
 */
static void EmitDrawElements_(bsp_leaf_t *leaf) {

	leaf->first_draw_elements = bsp_file.num_draw_elements;

	int32_t *lf = bsp_file.leaf_faces + leaf->first_leaf_face;

	qsort(lf, leaf->num_leaf_faces, sizeof(int32_t), EmitDrawElements_compare);

	for (int32_t i = 0; i < leaf->num_leaf_faces; i++) {

		if (bsp_file.num_draw_elements >= MAX_BSP_DRAW_ELEMENTS) {
			Com_Error(ERROR_FATAL, "MAX_BSP_LEAF_ELEMENTS\n");
		}

		bsp_draw_elements_t *draw = &bsp_file.draw_elements[bsp_file.num_draw_elements];
		bsp_file.num_draw_elements++;

		draw->texinfo = bsp_file.faces[*(lf + i)].texinfo;
		draw->lightmap = bsp_file.faces[*(lf + i)].lightmap.num;

		draw->first_element = bsp_file.num_elements;

		for (int32_t j = i; j < leaf->num_leaf_faces; j++, i++) {

			if (EmitDrawElements_compare(lf + i, lf + j)) {
				break;
			}

			const bsp_face_t *face = &bsp_file.faces[*(lf + j)];

			if (bsp_file.num_elements + face->num_elements >= MAX_BSP_ELEMENTS) {
				Com_Error(ERROR_FATAL, "MAX_BSP_ELEMENTS\n");
			}

			memcpy(bsp_file.elements + bsp_file.num_elements,
				   bsp_file.elements + face->first_element,
				   sizeof(int32_t) * face->num_elements);

			bsp_file.num_elements += face->num_elements;
		}

		draw->num_elements = bsp_file.num_elements - draw->first_element;
		assert(draw->num_elements);
	}

	leaf->num_draw_elements = bsp_file.num_draw_elements - leaf->first_draw_elements;
}

/**
 * @brief Emits draw elements for all leafs. This is called both for the BSP and LIGHT
 * compile phases, so care is taken to truncate the elements array and [re]allocate space.
 */
void EmitDrawElements(void) {

	// truncate the elements array, removing any stale draw elements
	const bsp_face_t *last_face = &bsp_file.faces[bsp_file.num_faces - 1];
	bsp_file.num_elements = last_face->first_element + last_face->num_elements;

	bsp_file.num_draw_elements = 0;

	Bsp_AllocLump(&bsp_file, BSP_LUMP_ELEMENTS, MAX_BSP_ELEMENTS);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_DRAW_ELEMENTS, MAX_BSP_DRAW_ELEMENTS);

	bsp_leaf_t *leaf = bsp_file.leafs;
	for (int32_t i = 0; i < bsp_file.num_leafs; i++, leaf++) {
		EmitDrawElements_(leaf);
	}

	Com_Verbose("%d draw elements\n", bsp_file.num_draw_elements);
}

/**
 * @brief
 */
static int32_t EmitLeaf(node_t *node) {

	if (bsp_file.num_leafs == MAX_BSP_LEAFS) {
		Com_Error(ERROR_FATAL, "MAX_BSP_LEAFS\n");
	}

	bsp_leaf_t *out = &bsp_file.leafs[bsp_file.num_leafs];
	bsp_file.num_leafs++;

	out->contents = node->contents;
	out->cluster = node->cluster;
	out->area = node->area;

	VectorCopy(node->mins, out->mins);
	VectorCopy(node->maxs, out->maxs);

	// write the leaf_brushes
	out->first_leaf_brush = bsp_file.num_leaf_brushes;

	for (const csg_brush_t *brush = node->brushes; brush; brush = brush->next) {

		if (bsp_file.num_leaf_brushes >= MAX_BSP_LEAF_BRUSHES) {
			Com_Error(ERROR_FATAL, "MAX_BSP_LEAF_BRUSHES\n");
		}

		const int32_t brush_num = (int32_t) (ptrdiff_t) (brush->original - brushes);

		int32_t i;
		for (i = out->first_leaf_brush; i < bsp_file.num_leaf_brushes; i++) {
			if (bsp_file.leaf_brushes[i] == brush_num) {
				break;
			}
		}

		if (i == bsp_file.num_leaf_brushes) {
			bsp_file.leaf_brushes[bsp_file.num_leaf_brushes] = brush_num;
			bsp_file.num_leaf_brushes++;
		}
	}

	out->num_leaf_brushes = bsp_file.num_leaf_brushes - out->first_leaf_brush;

	// write the leaf_faces
	if (out->contents & CONTENTS_SOLID) {
		return bsp_file.num_leafs - 1;
	}

	out->first_leaf_face = bsp_file.num_leaf_faces;

	int32_t s;
	for (const portal_t *p = node->portals; p; p = p->next[s]) {

		s = (p->nodes[1] == node);

		const face_t *face = p->face[s];
		if (!face) {
			continue; // not a visible portal
		}

		while (face->merged) {
			face = face->merged;
		}

		assert(face->num >= 0);
		assert(face->num <= bsp_file.num_faces);

		int32_t i;
		for (i = out->first_leaf_face; i < bsp_file.num_leaf_faces; i++) {
			if (bsp_file.leaf_faces[i] == face->num) {
				break;
			}
		}

		if (i == bsp_file.num_leaf_faces) {
			if (bsp_file.num_leaf_faces >= MAX_BSP_LEAF_FACES) {
				Com_Error(ERROR_FATAL, "MAX_BSP_LEAF_FACES\n");
			}

			bsp_file.leaf_faces[bsp_file.num_leaf_faces] = face->num;
			bsp_file.num_leaf_faces++;
		}
	}

	out->num_leaf_faces = bsp_file.num_leaf_faces - out->first_leaf_face;
	return bsp_file.num_leafs - 1;
}

/**
 * @brief
 */
static int32_t EmitNode(node_t *node) {

	if (node->plane_num == PLANE_NUM_LEAF) {
		Com_Error(ERROR_FATAL, "Leaf node\n");
	}

	if (node->plane_num & 1) {
		Com_Error(ERROR_FATAL, "Node referencing negative plane\n");
	}

	if (bsp_file.num_nodes == MAX_BSP_NODES) {
		Com_Error(ERROR_FATAL, "MAX_BSP_NODES\n");
	}

	bsp_node_t *out = &bsp_file.nodes[bsp_file.num_nodes];
	bsp_file.num_nodes++;

	VectorCopy(node->mins, out->mins);
	VectorCopy(node->maxs, out->maxs);

	out->plane_num = node->plane_num;
	out->first_face = bsp_file.num_faces;

	if (!node->faces) {
		c_nofaces++;
	} else {
		c_facenodes++;
	}

	for (face_t *f = node->faces; f; f = f->next) {
		EmitFace(f);
	}

	out->num_faces = bsp_file.num_faces - out->first_face;

	// recursively output the other nodes
	for (int32_t i = 0; i < 2; i++) {
		if (node->children[i]->plane_num == PLANE_NUM_LEAF) {
			out->children[i] = -(bsp_file.num_leafs + 1);
			EmitLeaf(node->children[i]);
		} else {
			out->children[i] = bsp_file.num_nodes;
			EmitNode(node->children[i]);
		}
	}

	return (int32_t) (ptrdiff_t) (out - bsp_file.nodes);
}

/**
 * @brief
 */
void EmitNodes(node_t *head_node) {

	c_nofaces = 0;
	c_facenodes = 0;

	Com_Verbose("--- WriteBSP ---\n");

	const int32_t old_faces = bsp_file.num_faces;
	const int32_t old_draw_elements = bsp_file.num_draw_elements;

	bsp_file.models[bsp_file.num_models].head_node = EmitNode(head_node);

	Com_Verbose("%5i nodes with faces\n", c_facenodes);
	Com_Verbose("%5i nodes without faces\n", c_nofaces);
	Com_Verbose("%5i faces\n", bsp_file.num_faces - old_faces);
	Com_Verbose("%5i draw elements\n", bsp_file.num_draw_elements - old_draw_elements);
}

/**
 * @brief
 */
static void EmitBrushes(void) {
	int32_t i, j, bnum, s, x;
	bsp_brush_t *db;
	brush_t *b;
	bsp_brush_side_t *cp;
	vec3_t normal;
	vec_t dist;
	int32_t plane_num;

	bsp_file.num_brush_sides = 0;
	bsp_file.num_brushes = num_brushes;

	for (bnum = 0; bnum < num_brushes; bnum++) {
		b = &brushes[bnum];
		db = &bsp_file.brushes[bnum];

		db->contents = b->contents;
		db->first_brush_side = bsp_file.num_brush_sides;
		db->num_sides = b->num_sides;

		for (j = 0; j < b->num_sides; j++) {

			if (bsp_file.num_brush_sides == MAX_BSP_BRUSH_SIDES) {
				Com_Error(ERROR_FATAL, "MAX_BSP_BRUSH_SIDES\n");
			}

			cp = &bsp_file.brush_sides[bsp_file.num_brush_sides];
			bsp_file.num_brush_sides++;

			cp->plane_num = b->original_sides[j].plane_num;
			cp->texinfo = b->original_sides[j].texinfo;
		}

		// add any axis planes not contained in the brush to bevel off corners
		for (x = 0; x < 3; x++) {
			for (s = -1; s <= 1; s += 2) {
				// add the plane
				VectorClear(normal);
				normal[x] = (vec_t) s;

				if (s == -1) {
					dist = -b->mins[x];
				} else {
					dist = b->maxs[x];
				}

				plane_num = FindPlane(normal, dist);

				for (i = 0; i < b->num_sides; i++)
					if (b->original_sides[i].plane_num == plane_num) {
						break;
					}

				if (i == b->num_sides) {
					if (bsp_file.num_brush_sides >= MAX_BSP_BRUSH_SIDES) {
						Com_Error(ERROR_FATAL, "MAX_BSP_BRUSH_SIDES\n");
					}

					bsp_file.brush_sides[bsp_file.num_brush_sides].plane_num = plane_num;
					bsp_file.brush_sides[bsp_file.num_brush_sides].texinfo = bsp_file.brush_sides[bsp_file.num_brush_sides - 1].texinfo;
					bsp_file.num_brush_sides++;
					db->num_sides++;
				}
			}
		}
	}
}

/**
 * @brief Generates the entity string from all retained entities.
 */
void EmitEntities(void) {

	Bsp_AllocLump(&bsp_file, BSP_LUMP_ENTITIES, MAX_BSP_ENT_STRING);

	char *out = bsp_file.entity_string;
	*out = '\0';

	for (int32_t i = 0; i < num_entities; i++) {
		const entity_key_value_t *e = entities[i].values;
		if (e) {
			g_strlcat(out, "{\n", MAX_BSP_ENT_STRING);
			while (e) {
				g_strlcat(out, va(" \"%s\" \"%s\"\n", e->key, e->value), MAX_BSP_ENT_STRING);
				e = e->next;
			}
			g_strlcat(out, "}\n", MAX_BSP_ENT_STRING);
		}
	}

	const size_t len = strlen(out);

	if (len == MAX_BSP_ENT_STRING - 1) {
		Com_Error(ERROR_FATAL, "MAX_BSP_ENT_STRING\n");
	}

	bsp_file.entity_string_size = (int32_t) len + 1;
}

/**
 * @brief
 */
void BeginBSPFile(void) {

	memset(&bsp_file, 0, sizeof(bsp_file));

	Bsp_AllocLump(&bsp_file, BSP_LUMP_TEXINFO, MAX_BSP_TEXINFO);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_PLANES, MAX_BSP_PLANES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_NODES, MAX_BSP_NODES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_LEAFS, MAX_BSP_LEAFS);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_LEAF_FACES, MAX_BSP_LEAF_FACES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_LEAF_BRUSHES, MAX_BSP_LEAF_BRUSHES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_DRAW_ELEMENTS, MAX_BSP_DRAW_ELEMENTS);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_BRUSHES, MAX_BSP_BRUSHES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_BRUSH_SIDES, MAX_BSP_BRUSH_SIDES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_VERTEXES, MAX_BSP_VERTEXES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_ELEMENTS, MAX_BSP_ELEMENTS);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_FACES, MAX_BSP_FACES);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_MODELS, MAX_BSP_MODELS);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_AREA_PORTALS, MAX_BSP_AREA_PORTALS);
	Bsp_AllocLump(&bsp_file, BSP_LUMP_AREAS, MAX_BSP_AREAS);

	/*
	 * jdolan 2019-01-01
	 *
	 * Leafs are referenced by their parents as child nodes, but with negative indices.
	 * Because zero can not be negated, the first leaf in the map must be padded here.
	 * You can choose to ignore this comment if you want to lose 3 days of your life
	 * to debugging PVS, like I did.
	 */
	bsp_file.num_leafs = 1;
	bsp_file.leafs[0].contents = CONTENTS_SOLID;
}

/**
 * @brief
 */
void EndBSPFile(void) {

	EmitDrawElements();
	EmitBrushes();
	EmitPlanes();
	EmitAreaPortals();
	EmitEntities();

	PhongVertexes();
}

/**
 * @brief
 */
void BeginModel(void) {

	if (bsp_file.num_models == MAX_BSP_MODELS) {
		Com_Error(ERROR_FATAL, "MAX_BSP_MODELS\n");
	}

	bsp_model_t *mod = &bsp_file.models[bsp_file.num_models];

	mod->first_face = bsp_file.num_faces;

	// bound the brushes
	const entity_t *e = &entities[entity_num];

	const int32_t start = e->first_brush;
	const int32_t end = start + e->num_brushes;

	vec3_t mins, maxs;
	ClearBounds(mins, maxs);

	for (int32_t j = start; j < end; j++) {
		const brush_t *b = &brushes[j];
		if (!b->num_sides) {
			continue; // not a real brush (origin brush)
		}
		AddPointToBounds(b->mins, mins, maxs);
		AddPointToBounds(b->maxs, mins, maxs);
	}

	VectorCopy(mins, mod->mins);
	VectorCopy(maxs, mod->maxs);
}

/**
 * @brief
 */
void EndModel(void) {
	bsp_model_t *mod = &bsp_file.models[bsp_file.num_models];

	mod->num_faces = bsp_file.num_faces - mod->first_face;

	bsp_file.num_models++;
}
