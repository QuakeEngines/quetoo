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

#ifndef __R_IMAGE_H__
#define __R_IMAGE_H__

#include "r_types.h"

r_image_t *R_LoadImage(const char *name, r_image_type_t type);

#ifdef __R_LOCAL_H__

extern r_image_t *r_mesh_shell_image;
extern r_image_t *r_warp_image;

void R_FilterImage(r_image_t *image, GLenum format, byte *data);
void R_UploadImage(r_image_t *image, GLenum format, byte *data);
void R_Screenshot_f(void);
void R_InitImages(void);
void R_ShutdownImages(void);

#endif /* __R_LOCAL_H__ */

#endif /* __R_IMAGE_H__ */
