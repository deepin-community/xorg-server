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
#ifndef RBO_H
#define RBO_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include "xf86drm.h"

/*describ a buffer objgpu whitch mem test needed */
struct rbo {
    int                 fd;
    unsigned            refcount;
    unsigned            mapcount;
    unsigned            handle;
    unsigned long long      size;
    unsigned            alignment;
    void                *data;
};

/*params users give*/
struct u_args {
    int gpu_fd;
    int driver_type;
    int set_env;
    int threshold;
    float coefficient;
    unsigned long long bo_alloc_size;
    unsigned long gpu_time;
    unsigned long cpu_time;
};

/*gpu driver types*/
enum DRIVER {
    RADEON = 1,
    AMDGPU,
    NVIDIA
};

/*gpu memory buffer types*/
enum MEM{
    CPU = 0x2,//GTT
    GPU = 0x4,//VRAM
};

struct rbo *rbo(struct u_args *m_a, unsigned type, unsigned handle, unsigned alignment);
int rbo_map(struct rbo *bo);
struct rbo *gbo(struct u_args *m_a, unsigned type, unsigned handle, unsigned alignment);
int gbo_map(struct rbo *bo);
void rbo_unmap(struct rbo *bo);
struct rbo *rbo_incref(struct rbo *bo);
struct rbo *rbo_decref(struct rbo *bo);
#endif
