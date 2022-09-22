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
#include "gpu_mem_rbo.h"

#define USAGE "\nUSAGE:\n\
                cmd\tgpu_mem_check 1048576 2 1\n\
                param\t1048576\ttest 1M gpu mem\n\
                param\t2\tthreshold to trigger save file action\n\
                param\t1\tsave to tmp\n"

/*return time in usec*/
static unsigned long get_time(void)
{
    struct timeval time;

    gettimeofday(&time,0);

    return (time.tv_sec * 1000000 + time.tv_usec);
}

static void real_test(struct u_args *m_a, int alloc_type, int driver_type)
{
    struct rbo *bo = NULL;
    char * v;
    unsigned long b_time = 0;

    if (m_a->driver_type == RADEON) {
        bo = rbo(m_a, alloc_type, 0, 0);
    } else if (m_a->driver_type == AMDGPU) {
        bo = gbo(m_a, alloc_type, 0, 0);
    } else {
        /*do nothing*/
    }

    if (!bo) {
        fprintf(stdout, "test %s mem failed\n",
                alloc_type == CPU ? "CPU" : "GPU" );
        return;
    }

    /*do write speed test*/
    v = bo->data;
    b_time = get_time();
    for (unsigned long long i=0; i<bo->size; i++) {
        v[i] = '\0';
    }

    if (alloc_type == CPU)
        m_a->cpu_time = get_time() - b_time;
    else
        m_a->gpu_time = get_time() - b_time;

    fprintf(stdout, "test %s mem successed | time[%ld]\n",
            alloc_type == CPU ? "CPU" : "GPU",
            alloc_type == CPU ? m_a->cpu_time : m_a->gpu_time);

    rbo_decref(bo);
}

static void do_memory_performance_test(struct u_args *m_a)
{
    if (m_a->driver_type == RADEON) {
        fprintf(stdout, "do radeon display mem test\n");

        fprintf(stdout, "testing CPU mem\n");
        real_test(m_a, CPU, RADEON);
        fprintf(stdout, "done\n");

        fprintf(stdout, "testing GPU mem\n");
        real_test(m_a, GPU, RADEON);
        fprintf(stdout, "done\n");
    } else if (m_a->driver_type == AMDGPU) {
        fprintf(stdout, "do amdgpu display mem test\n");

        fprintf(stdout, "testing CPU mem\n");
        real_test(m_a, CPU, AMDGPU);
        fprintf(stdout, "done\n");

        fprintf(stdout, "testing GPU mem\n");
        real_test(m_a, GPU, AMDGPU);
        fprintf(stdout, "done\n");
    } else {
        fprintf(stderr, "test not support\n");
        /*will support more*/
    }
}


/*open gpu device*/
static int radeon_open_fd(struct u_args *m_a)
{
    fprintf(stdout, "openning radeon display\n");
    m_a->gpu_fd = drmOpen("radeon", NULL);
    if (m_a->gpu_fd < 0) {
        fprintf(stderr, "open radeon display failed, ret=%d\n", m_a->gpu_fd);
    } else {
        m_a->driver_type = RADEON;
        fprintf(stdout, "radeon display opened, ret=%d\n", m_a->gpu_fd);
        return m_a->gpu_fd;
    }

    fprintf(stdout, "openning amdgpu display\n");
    m_a->gpu_fd = drmOpen("amdgpu", NULL);
    if (m_a->gpu_fd < 0) {
        fprintf(stderr, "open amdgpu display failed, ret=%d\n", m_a->gpu_fd);
    } else {
        m_a->driver_type = AMDGPU;
        fprintf(stdout, "amdgpu display opened, ret=%d\n", m_a->gpu_fd);
        return m_a->gpu_fd;
    }

    return m_a->gpu_fd;
}

/*get user prefer args, if not set ,will use default*/
static void getargs(int argc, char *argv[], struct u_args *m_a)
{
    /*get params*/
    m_a->bo_alloc_size = atol(argv[1]);

    /*the mem space to test*/
    if (m_a->bo_alloc_size <= 0) {
        fprintf(stderr, "use default memory size\n");
        m_a->bo_alloc_size = 1 * 1024 *1024;
    }
    fprintf(stdout, "allocate memory size: %llu\n", m_a->bo_alloc_size);

    /*get use prefer threshold*/
    m_a->threshold = atoi(argv[2]);
    if (m_a->threshold <= 0) {
        fprintf(stderr, "use default threshold\n");
        m_a->threshold = 2;
    }
    fprintf(stdout, "threshold :%d\n", m_a->threshold);

    /*whether save to file*/
    m_a->set_env = atoi(argv[3]);
    if (m_a->set_env <= 0) {
        fprintf(stderr, "coefficient abandon\n");
        m_a->set_env = 0;
    }
    fprintf(stdout, "coefficient save or not: %d\n", m_a->set_env);
}

int main(int argc, char *argv[])
{

    struct u_args *m_a;

    if (argc != 4) {
        fprintf(stderr, "%s\n", USAGE);
        return -1;
    }


    /*get user prefer params*/
    m_a = (struct u_args *)malloc(sizeof(struct u_args));
    memset(m_a, 0, sizeof(struct u_args));
    getargs(argc, argv, m_a);

    /*open gpu driver*/
    fprintf(stdout, "\n\ndo open display\n");
    m_a->gpu_fd = -1;
    radeon_open_fd(m_a);
    if (m_a->gpu_fd < 0) {
        fprintf(stderr, "failed to open radeon fd\n");
        return -1;
    }
    fprintf(stdout, "open display done\n");


    /*do the memory performance test*/
    fprintf(stdout, "\n\ndo mem test\n");
    do_memory_performance_test(m_a);
    fprintf(stdout, "mem test done\n");

    /*time CPU/GPU: performance of two memory*/
    m_a->coefficient = m_a->gpu_time/(float)m_a->cpu_time;

    printf("\nGPU/CPU[%ld/%ld] coefficient=[%f]\n", m_a->gpu_time, m_a->cpu_time, m_a->coefficient);

    /*close GPU*/
    close(m_a->gpu_fd);

    /*save the result*/
    if (m_a->set_env > 0) {
        if ((m_a->coefficient - m_a->threshold) > 0.01) {
            fprintf(stdout, "G coefficient - threshold = %f\n", m_a->coefficient - m_a->threshold);
            system("echo G > /lib/xorg/.fst");
        } else {
            fprintf(stdout, "V coefficient - threshold = %f\n", m_a->coefficient - m_a->threshold);
            system("echo V > /lib/xorg/.fst"); }
    }

    free(m_a);
    fprintf(stdout, "done\n");
    return 0;
}
