/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Clutter
 *
 * Copyright 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "clutter/clutter.h"

typedef struct {
  int x_origin, y_origin;
  float x_scale, y_scale;
} MetaTransforms;

gboolean meta_actor_vertices_are_untransformed (graphene_point3d_t *verts,
                                                float               widthf,
                                                float               heightf,
                                                MetaTransforms     *out_transforms);

gboolean meta_actor_painting_untransformed (CoglFramebuffer *fb,
                                            int              paint_width,
                                            int              paint_height,
                                            int              sample_widthf,
                                            int              sample_heightf,
                                            MetaTransforms  *out_transforms);
