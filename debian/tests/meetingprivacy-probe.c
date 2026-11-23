/*
 * Probe for the meeting-privacy mode, used by debian/tests/meetingprivacy.
 *
 *   probe wm                hold the WM_Sn selection, map a window, print its
 *                           id, stay alive
 *   probe prop V            set _DEEPIN_NET_MIRROR_MODE on the root window
 *   probe getimage W|root [xy]   read 16x16 pixels, print ALLOWED/DENIED
 *   probe own [xy]          map a window of our own and read it back
 *
 * Exit status is 0 for ALLOWED and 1 for DENIED so the shell test can assert
 * on either the text or the status.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

static int lastError;

static int
onError(Display *dpy, XErrorEvent *e)
{
    (void) dpy;
    lastError = e->error_code;
    return 0;
}

static int
readWindow(Display *dpy, Window win, int format)
{
    XImage *image;

    lastError = 0;
    image = XGetImage(dpy, win, 0, 0, 16, 16, AllPlanes, format);
    XSync(dpy, False);

    if (image) {
        XDestroyImage(image);
        return 0;
    }
    return lastError ? lastError : -1;
}

static int
paintRoot(Display *dpy, Window root)
{
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    XSetWindowAttributes attr;
    XColor colour;

    colour.red = 0xffff;
    colour.green = 0;
    colour.blue = 0;
    colour.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, cmap, &colour))
        return 0;

    attr.background_pixel = colour.pixel;
    XChangeWindowAttributes(dpy, root, CWBackPixel, &attr);
    XClearWindow(dpy, root);
    XSync(dpy, False);
    usleep(200000);
    return 1;
}

static void
printMean(XImage *image)
{
    unsigned long long sum[3] = { 0, 0, 0 };
    int x, y, n = 0;

    for (x = 0; x < 16; x++) {
        for (y = 0; y < 16; y++) {
            unsigned long pixel = XGetPixel(image, x, y);

            sum[0] += (pixel >> 16) & 0xff;
            sum[1] += (pixel >> 8) & 0xff;
            sum[2] += pixel & 0xff;
            n++;
        }
    }
    printf("%llu %llu %llu\n", sum[0] / n, sum[1] / n, sum[2] / n);
}

/* Reads through the shared memory path a screen recorder uses.  The segment is
 * left attached, the probe exits right after the read. */
static XImage *
readShm(Display *dpy, Window win)
{
    Screen *screen = DefaultScreenOfDisplay(dpy);
    XShmSegmentInfo shminfo;
    XImage *image;
    size_t size;

    memset(&shminfo, 0, sizeof(shminfo));
    image = XShmCreateImage(dpy, DefaultVisualOfScreen(screen),
                            (unsigned) DefaultDepthOfScreen(screen), ZPixmap,
                            NULL, &shminfo, 16, 16);
    if (!image)
        return NULL;

    size = (size_t) image->bytes_per_line * image->height;
    shminfo.shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (shminfo.shmid < 0)
        return NULL;

    shminfo.shmaddr = image->data = shmat(shminfo.shmid, NULL, 0);
    shminfo.readOnly = False;
    if (shminfo.shmaddr == (char *) -1 || !XShmAttach(dpy, &shminfo))
        return NULL;

    if (!XShmGetImage(dpy, win, image, 600, 400, AllPlanes)) {
        XSync(dpy, False);
        return NULL;
    }
    XSync(dpy, False);
    return image;
}

int
main(int argc, char **argv)
{
    Display *dpy = XOpenDisplay(NULL);
    Window root;

    if (!dpy) {
        fprintf(stderr, "probe: cannot open display\n");
        return 2;
    }
    root = DefaultRootWindow(dpy);
    XSetErrorHandler(onError);

    if (argc > 1 && !strcmp(argv[1], "wm")) {
        char name[32];
        Atom wm;
        Window win;

        snprintf(name, sizeof(name), "WM_S%d", DefaultScreen(dpy));
        wm = XInternAtom(dpy, name, False);
        XSetSelectionOwner(dpy, wm, root, CurrentTime);
        XSync(dpy, False);
        if (XGetSelectionOwner(dpy, wm) != root) {
            fprintf(stderr, "probe: cannot become the window manager\n");
            return 2;
        }
        win = XCreateSimpleWindow(dpy, root, 0, 0, 200, 200, 0, 0, 0xffffff);
        XMapWindow(dpy, win);
        XSync(dpy, False);
        printf("%lu\n", win);
        fflush(stdout);
        for (;;)
            pause();
    }

    if (argc > 2 && !strcmp(argv[1], "prop")) {
        Atom atom = XInternAtom(dpy, "_DEEPIN_NET_MIRROR_MODE", False);
        long value = atol(argv[2]);

        XChangeProperty(dpy, root, atom, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *) &value, 1);
        XSync(dpy, False);
        return 0;
    }

    if (argc > 2 && !strcmp(argv[1], "getimage")) {
        Window win = !strcmp(argv[2], "root")
            ? root : (Window) strtoul(argv[2], NULL, 0);
        int format = (argc > 3 && !strcmp(argv[3], "xy")) ? XYPixmap : ZPixmap;
        int code = readWindow(dpy, win, format);

        if (code == 0) {
            puts("ALLOWED");
            return 0;
        }
        printf("DENIED(code=%d)\n", code);
        return 1;
    }

    if (argc > 1 && !strcmp(argv[1], "own")) {
        int format = (argc > 2 && !strcmp(argv[2], "xy")) ? XYPixmap : ZPixmap;
        Window win = XCreateSimpleWindow(dpy, root, 0, 0, 200, 200, 0, 0, 0);
        int code;

        XMapWindow(dpy, win);
        XSync(dpy, False);
        usleep(200000);
        code = readWindow(dpy, win, format);
        if (code == 0) {
            puts("ALLOWED");
            return 0;
        }
        printf("DENIED(code=%d)\n", code);
        return 1;
    }

    if (argc > 1 && (!strcmp(argv[1], "screen") || !strcmp(argv[1], "shm"))) {
        int shm = !strcmp(argv[1], "shm");
        XImage *image;

        if (!paintRoot(dpy, root)) {
            fprintf(stderr, "probe: cannot paint the root window\n");
            return 2;
        }

        lastError = 0;
        image = shm ? readShm(dpy, root)
            : XGetImage(dpy, root, 600, 400, 16, 16, AllPlanes, ZPixmap);
        XSync(dpy, False);

        if (!image) {
            printf("DENIED(code=%d)\n", lastError);
            return 1;
        }
        printMean(image);
        if (!shm)
            XDestroyImage(image);
        return 0;
    }

    if (argc > 1 && !strcmp(argv[1], "attrs")) {
        XWindowAttributes attr;

        if (!XGetWindowAttributes(dpy, root, &attr)) {
            fprintf(stderr, "probe: cannot read attributes\n");
            return 2;
        }
        printf("%dx%d\n", attr.width, attr.height);
        return 0;
    }

    if (argc > 3 && !strcmp(argv[1], "setnet")) {
        long value[2];

        value[0] = strtol(argv[2], NULL, 10);
        value[1] = strtol(argv[3], NULL, 10);
        XChangeProperty(dpy, root,
                        XInternAtom(dpy, "_NET_DESKTOP_GEOMETRY", False),
                        XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *) value, 2);
        XSync(dpy, False);
        return 0;
    }

    if (argc > 1 && !strcmp(argv[1], "getnet")) {
        unsigned char *data = NULL;
        Atom type;
        int format;
        unsigned long count, after;

        if (XGetWindowProperty(dpy, root,
                               XInternAtom(dpy, "_NET_DESKTOP_GEOMETRY", False),
                               0, 2, False, XA_CARDINAL, &type, &format,
                               &count, &after, &data) != Success ||
            !data || count < 2) {
            fprintf(stderr, "probe: _NET_DESKTOP_GEOMETRY unreadable\n");
            return 2;
        }
        printf("%lux%lu\n", ((unsigned long *) data)[0],
               ((unsigned long *) data)[1]);
        return 0;
    }

    if (argc > 5 && !strcmp(argv[1], "setwork")) {
        long value[4];
        int i;

        for (i = 0; i < 4; i++)
            value[i] = strtol(argv[2 + i], NULL, 10);
        XChangeProperty(dpy, root, XInternAtom(dpy, "_NET_WORKAREA", False),
                        XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *) value, 4);
        XSync(dpy, False);
        return 0;
    }

    if (argc > 1 && !strcmp(argv[1], "getwork")) {
        unsigned char *data = NULL;
        Atom type;
        int format;
        unsigned long count, after;

        if (XGetWindowProperty(dpy, root, XInternAtom(dpy, "_NET_WORKAREA", False),
                               0, 4, False, XA_CARDINAL, &type, &format,
                               &count, &after, &data) != Success ||
            !data || count < 4) {
            fprintf(stderr, "probe: _NET_WORKAREA unreadable\n");
            return 2;
        }
        printf("%lu,%lu %lux%lu\n", ((unsigned long *) data)[0],
               ((unsigned long *) data)[1], ((unsigned long *) data)[2],
               ((unsigned long *) data)[3]);
        return 0;
    }

    fprintf(stderr, "usage: probe wm | prop V | getimage W|root [xy] | screen | shm | own [xy] | attrs | setnet W H | getnet | setwork X Y W H | getwork\n");
    return 2;
}
