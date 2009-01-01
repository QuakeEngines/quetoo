/*
 * Copyright(c) 1997-2001 Id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quake2World.
 * *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or(at your option) any later version.
 * *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * *
 * See the GNU General Public License for more details.
 * *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "qlight.h"

/*
 * 
 * every surface must be divided into at least two patches each axis
 * 
 */

patch_t *face_patches[MAX_BSP_FACES];
int num_patches;

vec3_t face_offset[MAX_BSP_FACES];	// for rotating bmodels

qboolean extrasamples;

vec3_t ambient;

float brightness = 1.0;
float saturation = 1.0;
float contrast = 1.0;

float surface_scale = 0.4;
float entity_scale = 1.0;


/*
 * Light_PointInLeafnum
 */
static int Light_PointInLeafnum(const vec3_t point){
	int nodenum;

	nodenum = 0;
	while(nodenum >= 0){
		dnode_t *node = &dnodes[nodenum];
		dplane_t *plane = &dplanes[node->planenum];
		vec_t dist = DotProduct(point, plane->normal) - plane->dist;
		if(dist > 0)
			nodenum = node->children[0];
		else
			nodenum = node->children[1];
	}

	return -nodenum - 1;
}


/*
 * Light_PointInLeaf
 */
dleaf_t *Light_PointInLeaf(const vec3_t point){
	const int num = Light_PointInLeafnum(point);
	return &dleafs[num];
}


/*
 * PvsForOrigin
 */
qboolean PvsForOrigin(const vec3_t org, byte *pvs){
	dleaf_t *leaf;

	if(!visdatasize){
		memset(pvs, 255, (numleafs + 7) / 8);
		return true;
	}

	leaf = Light_PointInLeaf(org);
	if(leaf->cluster == -1)
		return false;  // in solid leaf

	DecompressVis(dvisdata + dvis->bitofs[leaf->cluster][DVIS_PVS], pvs);
	return true;
}


extern int numcmodels;
extern cmodel_t map_cmodels[MAX_BSP_MODELS];
trace_t rad_trace;

/*
 * Light_Trace
 */
void Light_Trace(const vec3_t start, const vec3_t end, int mask){
	float frac;
	int i;

	frac = 9999.0;

	for(i = 0; i < numcmodels; i++){

		const trace_t tr = Cm_BoxTrace(start, end, vec3_origin, vec3_origin,
				map_cmodels[i].headnode, mask);

		if(tr.fraction < frac){
			frac = tr.fraction;
			rad_trace = tr;
		}
	}
}


/*
 * LightWorld
 */
static void LightWorld(void){
	int i;

	if(numnodes == 0 || numfaces == 0)
		Error("RadWorld: Empty map\n");

	// load the map for tracing
	Cm_LoadMap(bspname, &i);

	// turn each face into a single patch
	BuildPatches();

	// subdivide patches to a maximum dimension
	SubdividePatches();

	// create lights out of patches and entities
	BuildLights();

	// patches are no longer needed
	FreePatches();

	// build per-vertex normals for phong shading
	BuildVertexNormals();

	// build initial facelights
	RunThreadsOn(numfaces, true, BuildFacelights);

	// finalize it and write it out
	lightdatasize = 0;
	RunThreadsOn(numfaces, true, FinalLightFace);
}


/*
 * LIGHT_Main
 */
int LIGHT_Main(void){
	time_t start, end;
	int total_rad_time;

	#ifdef _WIN32
		char title[MAX_OSPATH];
		sprintf(title, "Q2WMap [Compiling LIGHT]");
		SetConsoleTitle(title);
	#endif

	Print("\n----- LIGHT -----\n\n");

	start = time(NULL);

	LoadBSPFile(bspname);

	if(!visdatasize)
		Error("No vis information\n");

	ParseEntities();

	CalcTextureReflectivity();

	LightWorld();

	WriteBSPFile(bspname);

	end = time(NULL);
	total_rad_time = (int)(end - start);
	Print("\nLIGHT Time: ");
	if(total_rad_time > 59)
		Print("%d Minutes ", total_rad_time / 60);
	Print("%d Seconds\n", total_rad_time % 60);

	return 0;
}
