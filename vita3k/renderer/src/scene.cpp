// Vita3K emulator project
// Copyright (C) 2021 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <gxm/functions.h>
#include <gxm/types.h>
#include <renderer/commands.h>
#include <renderer/state.h>
#include <renderer/types.h>

#include "driver_functions.h"
#include <renderer/gl/functions.h>
#include <renderer/gl/types.h>

#include <config/state.h>
#include <renderer/functions.h>
#include <util/log.h>

#define DEBUG_FRAMEBUFFER 1

#if DEBUG_FRAMEBUFFER
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif

namespace renderer {
COMMAND(handle_set_context) {
    const RenderTarget *rt = helper.pop<const RenderTarget *>();
    const SceGxmColorSurface *color_surface = helper.pop<SceGxmColorSurface *>();
    const SceGxmDepthStencilSurface *depth_stencil_surface = helper.pop<SceGxmDepthStencilSurface *>();

    if (rt) {
        render_context->current_render_target = rt;
    }

    if (color_surface && !color_surface->disabled) {
        render_context->record.color_surface = *color_surface;
        delete color_surface;
    } else {
        // Disable writing to this surface.
        // Data is still in render target though.
        render_context->record.color_surface.data = nullptr;
    }

    // Maybe we should disable writing to depth stencil too if it's null
    if (depth_stencil_surface) {
        render_context->record.depth_stencil_surface = *depth_stencil_surface;
        delete depth_stencil_surface;
    } else {
        render_context->record.depth_stencil_surface.depthData.reset();
        render_context->record.depth_stencil_surface.stencilData.reset();
    }

    switch (renderer.current_backend) {
    case Backend::OpenGL: {
        gl::set_context(static_cast<gl::GLState &>(renderer), *reinterpret_cast<gl::GLContext *>(render_context), mem, reinterpret_cast<const gl::GLRenderTarget *>(rt), features);
        break;
    }

    default:
        REPORT_MISSING(renderer.current_backend);
        break;
    }
}

COMMAND(handle_sync_surface_data) {
    SceGxmColorSurface *surface = &render_context->record.color_surface;
    if (helper.cmd->status) {
        surface = helper.pop<SceGxmColorSurface *>();
        if (!surface) {
            complete_command(renderer, helper, 1);
            return;
        }
    }

    if (renderer.disable_surface_sync) {
        if (helper.cmd->status)
            complete_command(renderer, helper, 0);
        return;
    }

    const size_t width = surface->width;
    const size_t height = surface->height;
    const size_t stride_in_pixels = surface->strideInPixels;
    const Address data = surface->data.address();
    uint32_t *const pixels = Ptr<uint32_t>(data).get(mem);

    // We protect the data to track syncing. If this is called then the data is definitely protected somehow.
    // We just unprotect and reprotect again :D
    const std::size_t total_size = height * gxm::get_stride_in_bytes(surface->colorFormat, stride_in_pixels);

    open_access_parent_protect_segment(mem, data);
    unprotect_inner(mem, data, total_size);

    switch (renderer.current_backend) {
    case Backend::OpenGL: {
        if (helper.cmd->status) {
            gl::lookup_and_get_surface_data(static_cast<gl::GLState &>(renderer), mem, *surface);
        } else {
            gl::get_surface_data(static_cast<gl::GLState &>(renderer), *reinterpret_cast<gl::GLContext *>(render_context), pixels, *surface);
        }

        break;
    }

    default:
        REPORT_MISSING(renderer.current_backend);
        break;
    }

#if DEBUG_FRAMEBUFFER
    if (data != 0 && config.color_surface_debug) {
        const std::string filename = fmt::format("color_surface_0x{:X}.png", data);

        // Assuming output is RGBA
        const int result = stbi_write_png(filename.c_str(), width, height, 4, pixels, stride_in_pixels * 4);

        if (!result) {
            LOG_TRACE("Fail to save color surface 0x{:X}", data);
        }
    }
#endif

    // Need to reprotect. In the case of explicit get, 100% chance it will be unlock later anyway.
    // No need to bother. Assumption of course.
    if (!helper.cmd->status && is_protecting(mem, data)) {
        protect_inner(mem, data, total_size, MEM_PERM_NONE);
    }

    close_access_parent_protect_segment(mem, data);

    if (helper.cmd->status) {
        complete_command(renderer, helper, 0);
    }
}

COMMAND(handle_draw) {
    SceGxmPrimitiveType type = helper.pop<SceGxmPrimitiveType>();
    SceGxmIndexFormat format = helper.pop<SceGxmIndexFormat>();
    void *indicies = helper.pop<void *>();
    const std::uint32_t count = helper.pop<const std::uint32_t>();
    const std::uint32_t instance_count = helper.pop<const std::uint32_t>();

    switch (renderer.current_backend) {
    case Backend::OpenGL: {
        gl::draw(static_cast<gl::GLState &>(renderer), *reinterpret_cast<gl::GLContext *>(render_context),
            features, type, format, indicies, count, instance_count, mem, base_path, title_id, self_name, config);

        break;
    }

    default: {
        REPORT_MISSING(renderer.current_backend);
        break;
    }
    }
}
} // namespace renderer
