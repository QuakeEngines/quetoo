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
#include "leakfile.h"
#include "map.h"
#include "material.h"
#include "portal.h"
#include "prtfile.h"
#include "tjunction.h"
#include "writebsp.h"
#include "qbsp.h"

float micro_volume = 0.125;

_Bool no_prune = false;
_Bool no_merge = false;
_Bool no_detail = false;
_Bool all_structural = false;
_Bool only_ents = false;
_Bool no_water = false;
_Bool no_csg = false;
_Bool no_weld = false;
_Bool no_share = false;
_Bool no_tjunc = false;
_Bool leak_test = false;
_Bool leaked = false;

int32_t block_xl = -8, block_xh = 7, block_yl = -8, block_yh = 7;

int32_t entity_num;

static node_t *block_nodes[10][10];

/**
 * @brief
 */
static node_t *BlockTree(int32_t xl, int32_t yl, int32_t xh, int32_t yh) {
	node_t *node;
	vec3_t normal;
	float dist;
	int32_t mid;

	if (xl == xh && yl == yh) {
		node = block_nodes[xl + 5][yl + 5];
		if (!node) { // return an empty leaf
			node = AllocNode();
			node->plane_num = PLANE_NUM_LEAF;
			node->contents = 0; //CONTENTS_SOLID;
			return node;
		}
		return node;
	}
	// create a separator along the largest axis
	node = AllocNode();

	if (xh - xl > yh - yl) { // split x axis
		mid = xl + (xh - xl) / 2 + 1;
		normal.x = 1;
		normal.y = 0;
		normal.z = 0;
		dist = mid * 1024;
		node->plane_num = FindPlane(normal, dist);
		node->children[0] = BlockTree(mid, yl, xh, yh);
		node->children[1] = BlockTree(xl, yl, mid - 1, yh);
	} else {
		mid = yl + (yh - yl) / 2 + 1;
		normal.x = 0;
		normal.y = 1;
		normal.z = 0;
		dist = mid * 1024;
		node->plane_num = FindPlane(normal, dist);
		node->children[0] = BlockTree(xl, mid, xh, yh);
		node->children[1] = BlockTree(xl, yl, xh, mid - 1);
	}

	return node;
}

static int32_t brush_start, brush_end;

/**
 * @brief
 */
static void ProcessBlock_Work(int32_t blocknum) {
	vec3_t mins, maxs;

	const int32_t yblock = block_yl + blocknum / (block_xh - block_xl + 1);
	const int32_t xblock = block_xl + blocknum % (block_xh - block_xl + 1);

	Com_Verbose("############### block %2i,%2i ###############\n", xblock, yblock);

	mins.x = xblock * 1024;
	mins.y = yblock * 1024;
	mins.z = MIN_WORLD_COORD;
	maxs.x = (xblock + 1) * 1024;
	maxs.y = (yblock + 1) * 1024;
	maxs.z = MAX_WORLD_COORD;

	// the brushes and chopbrushes could be cached between the passes...
	csg_brush_t *brushes = MakeBrushes(brush_start, brush_end, mins, maxs);
	if (!brushes) {
		node_t *node = AllocNode();
		node->plane_num = PLANE_NUM_LEAF;
		node->contents = CONTENTS_SOLID;
		block_nodes[xblock + 5][yblock + 5] = node;
		return;
	}

	if (!no_csg) {
		brushes = ChopBrushes(brushes);
	}

	tree_t *tree = BuildTree(brushes, mins, maxs);

	block_nodes[xblock + 5][yblock + 5] = tree->head_node;
}

/**
 * @brief
 */
static void ProcessWorldModel(void) {
	tree_t *tree = NULL;

	const entity_t *e = &entities[entity_num];

	brush_start = e->first_brush;
	brush_end = brush_start + e->num_brushes;

	// perform per-block operations
	if (block_xh * 1024 > map_maxs.x) {
		block_xh = floor(map_maxs.x / 1024.0);
	}
	if ((block_xl + 1) * 1024 < map_mins.x) {
		block_xl = floor(map_mins.x / 1024.0);
	}
	if (block_yh * 1024 > map_maxs.y) {
		block_yh = floor(map_maxs.y / 1024.0);
	}
	if ((block_yl + 1) * 1024 < map_mins.y) {
		block_yl = floor(map_mins.y / 1024.0);
	}

	if (block_xl < -4) {
		block_xl = -4;
	}
	if (block_yl < -4) {
		block_yl = -4;
	}
	if (block_xh > 3) {
		block_xh = 3;
	}
	if (block_yh > 3) {
		block_yh = 3;
	}

	for (int32_t optimize = 0; optimize <= 1; optimize++) {
		Com_Verbose("--------------------------------------------\n");

		const int32_t count = (block_xh - block_xl + 1) * (block_yh - block_yl + 1);

		Work(optimize ? "Optimizing tree" : "Creating tree", ProcessBlock_Work, count);

		// build the division tree
		// oversizing the blocks guarantees that all the boundaries
		// will also get nodes.

		Com_Verbose("--------------------------------------------\n");

		tree = AllocTree();
		tree->head_node = BlockTree(block_xl - 1, block_yl - 1, block_xh + 1, block_yh + 1);

		tree->mins.x = (block_xl) * 1024;
		tree->mins.y = (block_yl) * 1024;
		tree->mins.z = map_mins.z - 8;

		tree->maxs.x = (block_xh + 1) * 1024;
		tree->maxs.y = (block_yh + 1) * 1024;
		tree->maxs.z = map_maxs.z + 8;

		// perform the global operations
		MakeTreePortals(tree);

		if (FloodEntities(tree)) {
			FillOutside(tree->head_node);
		} else {
			Com_Warn("Map leaked, writing maps/%s.lin\n", map_base);
			leaked = true;

			LeakFile(tree);

			if (leak_test) {
				Com_Error(ERROR_FATAL, "Leak test failed");
			}
		}

		MarkVisibleSides(tree, brush_start, brush_end);
		if (leaked) {
			break;
		}
		if (!optimize) {
			FreeTree(tree);
		}
	}

	FloodAreas(tree);
	MakeTreeFaces(tree);

	if (!no_prune) {
		PruneNodes(tree->head_node);
	}

	if (!no_tjunc) {
		FixTJunctions(tree->head_node);
	}

	EmitNodes(tree->head_node);

	if (!leaked) {
		WritePortalFile(tree);
	}

	FreeTree(tree);
}

/**
 * @brief
 */
static void ProcessInlineModel(void) {

	const entity_t *e = &entities[entity_num];

	const int32_t start = e->first_brush;
	const int32_t end = start + e->num_brushes;

	vec3_t mins, maxs;

	mins.x = mins.y = mins.z = MIN_WORLD_COORD;
	maxs.x = maxs.y = maxs.z = MAX_WORLD_COORD;

	csg_brush_t *brushes = MakeBrushes(start, end, mins, maxs);
	if (!no_csg) {
		brushes = ChopBrushes(brushes);
	}

	tree_t *tree = BuildTree(brushes, mins, maxs);

	MakeTreePortals(tree);
	MarkVisibleSides(tree, start, end);
	MakeTreeFaces(tree);

	if (!no_prune) {
		PruneNodes(tree->head_node);
	}

	if (!no_tjunc) {
		FixTJunctions(tree->head_node);
	}

	EmitNodes(tree->head_node);
	FreeTree(tree);
}

/**
 * @brief
 */
static void ProcessModels(void) {

	for (entity_num = 0; entity_num < num_entities; entity_num++) {

		if (!entities[entity_num].num_brushes) {
			continue;
		}

		Com_Verbose("############### model %i ###############\n", bsp_file.num_models);
		BeginModel();
		if (entity_num == 0) {
			ProcessWorldModel();
		} else {
			ProcessInlineModel();
		}
		EndModel();
	}
}

/**
 * @brief
 */
int32_t BSP_Main(void) {

	Com_Print("\n------------------------------------------\n");
	Com_Print("\nCompiling %s from %s\n\n", bsp_name, map_name);

	const uint32_t start = SDL_GetTicks();

	LoadMaterials(va("maps/%s.mat", map_base), ASSET_CONTEXT_TEXTURES, NULL);

	if (only_ents) {

		LoadBSPFile(bsp_name, BSP_LUMPS_ALL);

		LoadMapFile(map_name);

		EmitEntities();
	} else {

		Fs_Delete(va("maps/%s.prt", map_base));
		Fs_Delete(va("maps/%s.lin", map_base));

		BeginBSPFile();

		LoadMapFile(map_name);

		ProcessModels();

		EndBSPFile();
	}

	WriteBSPFile(va("maps/%s.bsp", map_base));

	FreeMaterials();

	FreeWindings();

	for (int32_t tag = MEM_TAG_QBSP; tag < MEM_TAG_QVIS; tag++) {
		Mem_FreeTag(tag);
	}

	const uint32_t end = SDL_GetTicks();
	Com_Print("\nCompiled %s in %d ms\n", bsp_name, (end - start));

	return 0;
}
