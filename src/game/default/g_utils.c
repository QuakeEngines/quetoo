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

#include "g_local.h"


/*
 * G_ProjectSource
 */
void G_ProjectSource(vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result){
	result[0] = point[0] + forward[0] * distance[0] + right[0] * distance[1];
	result[1] = point[1] + forward[1] * distance[0] + right[1] * distance[1];
	result[2] = point[2] + forward[2] * distance[0] + right[2] * distance[1] + distance[2];
}


/*
 * G_Find
 * 
 * Searches all active entities for the next one that holds
 * the matching string at fieldofs(use the FOFS() macro) in the structure.
 * 
 * Searches beginning at the edict after from, or the beginning if NULL
 * NULL will be returned if the end of the list is reached.
 * 
 */
edict_t *G_Find(edict_t *from, int fieldofs, const char *match){
	char *s;

	if(!from)
		from = g_edicts;
	else
		from++;

	for(; from < &g_edicts[globals.num_edicts]; from++){
		if(!from->inuse)
			continue;
		s = *(char **)((byte *)from + fieldofs);
		if(!s)
			continue;
		if(!strcasecmp(s, match))
			return from;
	}

	return NULL;
}


/*
 * G_FindRadius
 * 
 * Returns entities that have origins within a spherical area
 * 
 * G_FindRadius(origin, radius)
 */
edict_t *G_FindRadius(edict_t *from, vec3_t org, float rad){
	vec3_t eorg;
	int j;

	if(!from)
		from = g_edicts;
	else
		from++;
	for(; from < &g_edicts[globals.num_edicts]; from++){
		if(!from->inuse)
			continue;
		if(from->solid == SOLID_NOT)
			continue;
		for(j = 0; j < 3; j++)
			eorg[j] = org[j] -(from->s.origin[j] +(from->mins[j] + from->maxs[j]) * 0.5);
		if(VectorLength(eorg) > rad)
			continue;
		return from;
	}

	return NULL;
}


/*
 * G_PickTarget
 * 
 * Searches all active entities for the next one that holds
 * the matching string at fieldofs(use the FOFS() macro) in the structure.
 * 
 * Searches beginning at the edict after from, or the beginning if NULL
 * NULL will be returned if the end of the list is reached.
 * 
 */
#define MAXCHOICES	8

edict_t *G_PickTarget(char *targetname){
	edict_t *ent = NULL;
	int num_choices = 0;
	edict_t *choice[MAXCHOICES];

	if(!targetname){
		gi.Dprintf("G_PickTarget called with NULL targetname\n");
		return NULL;
	}

	while(true){
		ent = G_Find(ent, FOFS(targetname), targetname);
		if(!ent)
			break;
		choice[num_choices++] = ent;
		if(num_choices == MAXCHOICES)
			break;
	}

	if(!num_choices){
		gi.Dprintf("G_PickTarget: target %s not found\n", targetname);
		return NULL;
	}

	return choice[rand() % num_choices];
}



static void Think_Delay(edict_t *ent){
	G_UseTargets(ent, ent->activator);
	G_FreeEdict(ent);
}


/*
 * G_UseTargets
 * 
 * the global "activator" should be set to the entity that initiated the firing.
 * 
 * If self.delay is set, a DelayedUse entity will be created that will actually
 * do the SUB_UseTargets after that many seconds have passed.
 * 
 * Centerprints any self.message to the activator.
 * 
 * Search for(string)targetname in all entities that
 * match(string)self.target and call their .use function
 * 
 */
void G_UseTargets(edict_t *ent, edict_t *activator){
	edict_t *t;

	// check for a delay
	if(ent->delay){
		// create a temp object to fire at a later time
		t = G_Spawn();
		t->classname = "DelayedUse";
		t->nextthink = level.time + ent->delay;
		t->think = Think_Delay;
		t->activator = activator;
		if(!activator)
			gi.Dprintf("Think_Delay with no activator\n");
		t->message = ent->message;
		t->target = ent->target;
		t->killtarget = ent->killtarget;
		return;
	}

	// print the message
	if((ent->message) && activator->client){
		gi.Cnprintf(activator, "%s", ent->message);
		if(ent->noise_index)
			gi.Sound(activator, CHAN_AUTO, ent->noise_index, 1, ATTN_NORM, 0);
		else
			gi.Sound(activator, CHAN_AUTO, gi.SoundIndex("misc/chat.wav"), 1, ATTN_NORM, 0);
	}

	// kill killtargets
	if(ent->killtarget){
		t = NULL;
		while((t = G_Find(t, FOFS(targetname), ent->killtarget))){
			G_FreeEdict(t);
			if(!ent->inuse){
				gi.Dprintf("entity was removed while using killtargets\n");
				return;
			}
		}
	}

	// fire targets
	if(ent->target){
		t = NULL;
		while((t = G_Find(t, FOFS(targetname), ent->target))){
			// doors fire area portals in a specific way
			if(!strcasecmp(t->classname, "func_areaportal") &&
					(!strcasecmp(ent->classname, "func_door") || !strcasecmp(ent->classname, "func_door_rotating")))
				continue;

			if(t == ent){
				gi.Dprintf("entity asked to use itself\n");
			} else {
				if(t->use)
					t->use(t, ent, activator);
			}
			if(!ent->inuse){
				gi.Dprintf("entity was removed while using targets\n");
				return;
			}
		}
	}
}


/*
 * tv
 * 
 * This is just a convenience function
 * for making temporary vectors for function calls
 */
float *tv(float x, float y, float z){
	static int index;
	static vec3_t vecs[8];
	float *v;

	// use an array so that multiple tempvectors won't collide
	// for a while
	v = vecs[index];
	index = (index + 1) & 7;

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return v;
}


/*
 * vtos
 * 
 * A convenience function for printing vectors.
 */
char *vtos(vec3_t v){
	static int index;
	static char str[8][32];
	char *s;

	// use an array so that multiple vtos won't collide
	s = str[index];
	index = (index + 1) & 7;

	snprintf(s, 32, "(%i %i %i)", (int)v[0],(int)v[1],(int)v[2]);

	return s;
}


vec3_t VEC_UP = {0, -1, 0};
vec3_t MOVEDIR_UP = {0, 0, 1};
vec3_t VEC_DOWN	= {0, -2, 0};
vec3_t MOVEDIR_DOWN	= {0, 0, -1};

void G_SetMovedir(vec3_t angles, vec3_t movedir){
	if(VectorCompare(angles, VEC_UP)){
		VectorCopy(MOVEDIR_UP, movedir);
	} else if(VectorCompare(angles, VEC_DOWN)){
		VectorCopy(MOVEDIR_DOWN, movedir);
	} else {
		AngleVectors(angles, movedir, NULL, NULL);
	}

	VectorClear(angles);
}


char *G_CopyString(char *in){
	char *out;

	out = gi.TagMalloc(strlen(in) + 1, TAG_LEVEL);
	strcpy(out, in);
	return out;
}


void G_InitEdict(edict_t *e){
	e->inuse = true;
	e->classname = "noclass";
	e->gravity = 1.0;
	e->timestamp = level.time;
	e->s.number = e - g_edicts;
}


/*
 * G_Spawn
 * 
 * Either finds a free edict, or allocates a new one.
 * Try to avoid reusing an entity that was recently freed, because it
 * can cause the client to think the entity morphed into something else
 * instead of being removed and recreated, which can cause interpolated
 * angles and bad trails.
 */
edict_t *G_Spawn(void){
	int i;
	edict_t *e;

	e = &g_edicts[(int)sv_maxclients->value + 1];
	for(i = sv_maxclients->value + 1; i < globals.num_edicts; i++, e++){
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if(!e->inuse && (e->freetime < 2 || level.time - e->freetime > 0.5)){
			G_InitEdict(e);
			return e;
		}
	}

	if(i >= g_maxentities->value)
		gi.Error("G_Spawn: No free edicts.");

	globals.num_edicts++;
	G_InitEdict(e);
	return e;
}


/*
 * G_FreeEdict
 * 
 * Marks the edict as free
 */
void G_FreeEdict(edict_t *ed){
	gi.UnlinkEntity(ed);  // unlink from world

	if((ed - g_edicts) <= sv_maxclients->value)
		return;

	memset(ed, 0, sizeof(*ed));
	ed->classname = "freed";
	ed->freetime = level.time;
	ed->inuse = false;
}


/*
 * G_TouchTriggers
 */
void G_TouchTriggers(edict_t *ent){
	int i, num;
	edict_t *touch[MAX_EDICTS], *hit;

	if(!ent->client || ent->health <= 0)
		return;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_TRIGGERS);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it(killtriggered)
	for(i = 0; i < num; i++){
		hit = touch[i];
		if(!hit->inuse)
			continue;
		if(!hit->touch)
			continue;
		hit->touch(hit, ent, NULL, NULL);
	}
}


/*
 * G_TouchSolids
 * 
 * Call after linking a new trigger in during gameplay
 * to force all entities it covers to immediately touch it
 */
void G_TouchSolids(edict_t *ent){
	int i, num;
	edict_t *touch[MAX_EDICTS], *hit;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_SOLID);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it(killtriggered)
	for(i = 0; i < num; i++){
		hit = touch[i];
		if(!hit->inuse)
			continue;
		if(ent->touch)
			ent->touch(hit, ent, NULL, NULL);
		if(!ent->inuse)
			break;
	}
}


/*
 * G_KillBox
 * 
 * Kills all entities that would touch the proposed new positioning
 * of ent.  Ent should be unlinked before calling this!
 */
qboolean G_KillBox(edict_t *ent){
	trace_t tr;

	while(true){
		tr = gi.Trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, NULL, MASK_PLAYERSOLID);
		if(!tr.ent)
			break;

		// nail it
		G_Damage(tr.ent, ent, ent, vec3_origin, ent->s.origin, vec3_origin,
				100000, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);

		// if we didn't kill it, fail
		if(tr.ent->solid)
			return false;
	}

	return true;  // all clear
}


/*
 * G_GameplayName
 */
char *G_GameplayName(int g){
	switch(g){
		case INSTAGIB:
			return "INSTAGIB";
		case ARENA:
			return "ARENA";
		default:
			return "DEATHMATCH";
	}
}


/*
 * G_GameplayByName
 */
int G_GameplayByName(char *c){

	if(!c || !strlen(c))
		return DEATHMATCH;

	switch(*c){  // hack for numeric matches, atoi wont cut it
		case '0':
			return DEATHMATCH;
		case '1':
			return INSTAGIB;
		case '2':
			return ARENA;
		default:
			break;
	}

	if(strstr(Com_Lowercase(c), "insta"))
		return INSTAGIB;
	if(strstr(Com_Lowercase(c), "arena"))
		return ARENA;
	return DEATHMATCH;
}


/*
 * G_TeamByName
 */
team_t *G_TeamByName(char *c){

	if(!c || !*c)
		return NULL;

	if(!strcasecmp(good.name, c))
		return &good;
	if(!strcasecmp(evil.name, c))
		return &evil;

	return NULL;
}


/*
 * G_TeamForFlag
 */
team_t *G_TeamForFlag(edict_t *ent){

	if(!level.ctf)
		return NULL;

	if(!ent->item || !(ent->item->flags & IT_FLAG))
		return NULL;

	if(!strcmp(ent->classname, "item_flag_team1"))
		return &good;

	if(!strcmp(ent->classname, "item_flag_team2"))
		return &evil;

	return NULL;
}


/*
 * G_FlagForTeam
 */
edict_t *G_FlagForTeam(team_t *t){
	edict_t *ent;
	char class[32];
	int i;

	if(!level.ctf)
		return NULL;

	if(t != &good && t != &evil)
		return NULL;

	strcpy(class, (t == &good ? "item_flag_team1" : "item_flag_team2"));

	i = (int)sv_maxclients->value + 1;
	while(i < globals.num_edicts){

		ent = &globals.edicts[i++];

		if(!ent->item || !(ent->item->flags & IT_FLAG))
			continue;

		// when a carrier is killed, we spawn a new temporary flag
		// where they died.  we are generally not interested in these.
		if(ent->spawnflags & SF_ITEM_DROPPED)
			continue;

		if(!strcmp(ent->classname, class))
			return ent;
	}

	return NULL;
}


/*
 * G_EffectForTeam
 */
int G_EffectForTeam(team_t *t){

	if(!t)
		return 0;

	return (t == &good ? EF_CTF_BLUE : EF_CTF_RED);
}


/*
 * G_OtherTeam
 */
team_t *G_OtherTeam(team_t *t){

	if(!t)
		return NULL;

	if(t == &good)
		return &evil;

	if(t == &evil)
		return &good;

	return NULL;
}


/*
 * G_SmallestTeam
 */
team_t *G_SmallestTeam(void){
	int i, g, e;
	gclient_t *cl;

	g = e = 0;

	for(i = 0; i < sv_maxclients->value; i++){
		if(!g_edicts[i + 1].inuse)
			continue;

		cl = g_edicts[i + 1].client;

		if(cl->locals.team == &good)
			g++;
		else if(cl->locals.team == &evil)
			e++;
	}

	return g < e ? &good : &evil;
}


/*
 * G_ClientByName
 */
gclient_t *G_ClientByName(char *name){
	int i, j, min;
	gclient_t *cl, *ret;

	if(!name)
		return NULL;

	ret = NULL;
	min = 9999;

	for(i = 0; i < sv_maxclients->value; i++){
		if(!g_edicts[i + 1].inuse)
			continue;

		cl = g_edicts[i + 1].client;
		if((j = strcmp(name, cl->locals.netname)) < min){
			ret = cl;
			min = j;
		}
	}

	return ret;
}


/*
 * G_IsStationary
 */
qboolean G_IsStationary(edict_t *ent){

	if(!ent)
		return false;

	return VectorCompare(vec3_origin, ent->velocity);
}

