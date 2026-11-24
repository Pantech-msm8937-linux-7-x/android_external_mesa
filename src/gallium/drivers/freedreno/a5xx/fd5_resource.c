/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "drm-uapi/drm_fourcc.h"

#include "fd5_resource.h"
#include "a5xx.xml.h"

static void
setup_lrz(struct fd_resource *rsc)
{
   struct fd_screen *screen = fd_screen(rsc->b.b.screen);
   unsigned lrz_pitch = align(DIV_ROUND_UP(rsc->b.b.width0, 8), 64);
   unsigned lrz_height = DIV_ROUND_UP(rsc->b.b.height0, 8);

   /* LRZ buffer is super-sampled: */
   switch (rsc->b.b.nr_samples) {
   case 4:
      lrz_pitch *= 2;
      FALLTHROUGH;
   case 2:
      lrz_height *= 2;
   }

   unsigned size = lrz_pitch * lrz_height * 2;

   size += 0x1000; /* for GRAS_LRZ_FAST_CLEAR_BUFFER */

   rsc->lrz_height = lrz_height;
   rsc->lrz_width = lrz_pitch;
   rsc->lrz_pitch = lrz_pitch;
   rsc->lrz = fd_bo_new(screen->dev, size, FD_BO_NOMAP, "lrz");
}

uint32_t
fd5_setup_slices(struct fd_resource *rsc)
{
   struct pipe_resource *prsc = &rsc->b.b;

   if (FD_DBG(LRZ) && has_depth(prsc->format) && !is_z32(prsc->format))
      setup_lrz(rsc);

   fdl5_layout(&rsc->layout, prsc->format, fd_resource_nr_samples(prsc),
               prsc->width0, prsc->height0, prsc->depth0, prsc->last_level + 1,
               prsc->array_size, prsc->target == PIPE_TEXTURE_3D);

   return rsc->layout.size;
}

/* UBWC, of the buffers gralloc (minigbm) makes for the window and the
 * composition: of the colour formats of 32 bits a texel, the flags first
 * (see fdl5_layout()), as the Vulkan driver has it
 */
static bool
ok_ubwc_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8X8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_B8G8R8X8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_SRGB:
   case PIPE_FORMAT_R8G8B8X8_SRGB:
   case PIPE_FORMAT_B8G8R8A8_SRGB:
   case PIPE_FORMAT_B8G8R8X8_SRGB:
      return true;
   default:
      return false;
   }
}

static int
fd5_layout_resource_for_modifier(struct fd_resource *rsc, uint64_t modifier)
{
   struct pipe_resource *prsc = &rsc->b.b;

   switch (modifier) {
   case DRM_FORMAT_MOD_QCOM_COMPRESSED: {
      uint32_t pitch = rsc->layout.pitch0;

      if (!ok_ubwc_format(prsc->format) || prsc->target != PIPE_TEXTURE_2D ||
          prsc->last_level || prsc->array_size > 1 || prsc->nr_samples > 1)
         return -1;

      rsc->layout.ubwc = true;
      rsc->layout.tile_mode = TILE5_3;
      rsc->layout.tile_all = true;
      rsc->layout.size = 0;
      fdl5_layout(&rsc->layout, prsc->format, 1, prsc->width0, prsc->height0,
                  1, 1, 1, false);

      if (fdl_pitch(&rsc->layout, 0) != pitch ||
          rsc->layout.size > fd_bo_size(rsc->bo))
         return -1;
      return 0;
   }
   case DRM_FORMAT_MOD_LINEAR:
   case DRM_FORMAT_MOD_INVALID:
      return 0;
   default:
      return -1;
   }
}

static bool
fd5_is_format_supported(struct pipe_screen *pscreen, enum pipe_format format,
                        uint64_t modifier)
{
   switch (modifier) {
   case DRM_FORMAT_MOD_LINEAR:
      return true;
   case DRM_FORMAT_MOD_QCOM_COMPRESSED:
      return ok_ubwc_format(format);
   default:
      return false;
   }
}

void
fd5_resource_screen_init(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   screen->layout_resource_for_modifier = fd5_layout_resource_for_modifier;
   screen->is_format_supported = fd5_is_format_supported;
}
