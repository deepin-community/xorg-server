/*
 * Copyright © 2011 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    huang jianghui <huangjianghui@uniontech.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <xf86drm.h>
#include <drm/radeon_drm.h>
#include <drm/amdgpu_drm.h>
#include "gpu_mem_rbo.h"

/*require amdgpu memory from gpu ttm and map it*/
struct rbo *gbo(struct u_args *m_a, unsigned type, unsigned handle, unsigned alignment)
{
    struct rbo *bo;
    int r;

    bo = calloc(1, sizeof(*bo));
    if (bo == NULL) {
        return NULL;
    }
    bo->fd = m_a->gpu_fd;
    bo->size = m_a->bo_alloc_size;
    bo->handle = handle;
    bo->refcount = 1;
    bo->alignment = alignment;

    if (handle) {
        struct drm_gem_open open_arg;

        memset(&open_arg, 0, sizeof(open_arg));
        open_arg.name = bo->handle;
        r = drmIoctl(bo->fd, DRM_IOCTL_GEM_OPEN, &open_arg);
        if (r != 0) {
            free(bo);
            return NULL;
        }
        bo->handle = open_arg.handle;
    } else {

        union drm_amdgpu_gem_create args;

        memset(&args, 0, sizeof(args));
        args.in.bo_size = bo->size;
        args.in.alignment = bo->alignment;

        /* Set the placement. */
        args.in.domains = type;
        args.in.domain_flags = 0;


        /*get a bo object handle*/
        r = drmCommandWriteRead(bo->fd, DRM_AMDGPU_GEM_CREATE,
                                &args, sizeof(args));

        bo->handle = args.out.handle;
        if (r) {
            fprintf(stderr, "Failed to allocate :\n");
            fprintf(stderr, "   size      : %llu bytes\n", bo->size);
            fprintf(stderr, "   alignment : %u bytes\n", bo->alignment);
            free(bo);
            return NULL;
        }
    }

    /*map gpu address, later can do write test*/
    if (gbo_map(bo)) {
        fprintf(stderr, "%s failed to copy data into bo\n", __func__);
        return rbo_decref(bo);
    }

    return bo;
}

/*require radeon gpu memory from gpu ttm and map it*/
struct rbo *rbo(struct u_args *m_a, unsigned type, unsigned handle, unsigned alignment)
{
    struct rbo *bo;
    int r;

    bo = calloc(1, sizeof(*bo));
    if (bo == NULL) {
        return NULL;
    }
    bo->fd = m_a->gpu_fd;
    bo->size = m_a->bo_alloc_size;
    bo->handle = handle;
    bo->refcount = 1;
    bo->alignment = alignment;

    if (handle) {
        struct drm_gem_open open_arg;

        memset(&open_arg, 0, sizeof(open_arg));
        open_arg.name = handle;
        r = drmIoctl(bo->fd, DRM_IOCTL_GEM_OPEN, &open_arg);
        if (r != 0) {
            free(bo);
            return NULL;
        }
        bo->handle = open_arg.handle;
    } else {
        struct drm_radeon_gem_create args;

        args.size = bo->size;
        args.alignment = bo->alignment;
        args.initial_domain = type;
        args.flags = 0;
        args.handle = 0;

        /*get a bo object handle*/
        r = drmCommandWriteRead(bo->fd, DRM_RADEON_GEM_CREATE,
                                &args, sizeof(args));
        bo->handle = args.handle;
        if (r) {
            fprintf(stderr, "Failed to allocate :\n");
            fprintf(stderr, "   size      : %llu bytes\n", bo->size);
            fprintf(stderr, "   alignment : %u bytes\n", bo->alignment);
            free(bo);
            return NULL;
        }
    }

    /*map gpu address, later can do write test*/
    if (rbo_map(bo)) {
        fprintf(stderr, "%s failed to copy data into bo\n", __func__);
        return rbo_decref(bo);
    }

    return bo;
}

/*map amdgpu gpu memory addr*/
int gbo_map(struct rbo *bo)
{
	union drm_amdgpu_gem_mmap args;
	void *ptr;
	int r;

    if (bo->mapcount++ != 0) {
        return 0;
    }

    memset(&args, 0, sizeof(args));
	args.in.handle = bo->handle;
    r = drmCommandWriteRead(bo->fd, DRM_AMDGPU_GEM_MMAP,
                            &args, sizeof(args));
    if (r) {
        fprintf(stderr, "error mapping %p 0x%08X (error = %d)\n",
            bo, bo->handle, r);
        return r;
    }
    ptr = mmap(0, bo->size, PROT_READ|PROT_WRITE, MAP_SHARED, bo->fd, args.out.addr_ptr);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "%s failed to map bo\n", __func__);
        return -errno;
    }
    bo->data = ptr;
    return 0;
}

/*map radeon gpu memory addr*/
int rbo_map(struct rbo *bo)
{
    struct drm_radeon_gem_mmap args;
    void *ptr;
    int r;

    if (bo->mapcount++ != 0) {
        return 0;
    }
    /* Zero out args to make valgrind happy */
    memset(&args, 0, sizeof(args));
    args.handle = bo->handle;
    args.offset = 0;
    args.size = (uint64_t)bo->size;
    r = drmCommandWriteRead(bo->fd, DRM_RADEON_GEM_MMAP,
                            &args, sizeof(args));
    if (r) {
        fprintf(stderr, "error mapping %p 0x%08X (error = %d)\n",
            bo, bo->handle, r);
        return r;
    }
    ptr = mmap(0, args.size, PROT_READ|PROT_WRITE, MAP_SHARED, bo->fd, args.addr_ptr);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "%s failed to map bo\n", __func__);
        return -errno;
    }
    bo->data = ptr;
    return 0;
}

/*unmap gpu memory addr*/
void rbo_unmap(struct rbo *bo)
{
    if (--bo->mapcount > 0) {
        return;
    }
    munmap(bo->data, bo->size);
    bo->data = NULL;
}

struct rbo *rbo_incref(struct rbo *bo)
{
    bo->refcount++;
    return bo;
}

struct rbo *rbo_decref(struct rbo *bo)
{
    struct drm_gem_close args;

    if (bo == NULL)
        return NULL;
    if (--bo->refcount > 0) {
        return NULL;
    }

    munmap(bo->data, bo->size);
    memset(&args, 0, sizeof(args));
    args.handle = bo->handle;
    drmIoctl(bo->fd, DRM_IOCTL_GEM_CLOSE, &args);
    memset(bo, 0, sizeof(struct rbo));
    free(bo);
    return NULL;
}
