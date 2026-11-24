/*
 * Probe for the meeting-privacy mode, used by debian/tests/meetingprivacy.
 *
 *   probe wm                hold the WM_Sn selection, map a window, print its
 *                           id, stay alive
 *   probe wmgetimage W      become the window manager and read W
 *   probe prop V            set _DEEPIN_NET_MIRROR_MODE on the root window
 *   probe getimage W|root [xy]   read 16x16 pixels, print ALLOWED/DENIED
 *   probe own [xy]          map a window of our own and read it back
 *   probe screen [X Y [xy]] read 16x16 pixels of the painted root window
 *   probe shm [X Y [xy]]    the same through XShmGetImage
 *   probe copyarea S        CopyArea 16x16 out of S (root, own or a window id)
 *   probe copyplane S       CopyPlane the same way
 *   probe geom              print the root window geometry
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

/* Exit status and text for a read: 0/ALLOWED or 1/DENIED(code) */
static int
report(int code)
{
    if (code == 0) {
        puts("ALLOWED");
        return 0;
    }
    printf("DENIED(code=%d)\n", code);
    return 1;
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

/* Move 16x16 pixels of S into a pixmap of our own, the way a screenshot tool
 * that does not use GetImage does it. */
static int
copyWindow(Display *dpy, Window win, int plane)
{
    Screen *screen = DefaultScreenOfDisplay(dpy);
    Pixmap pixmap;
    GC gc;
    int status;

    lastError = 0;
    pixmap = XCreatePixmap(dpy, win, 16, 16, DefaultDepthOfScreen(screen));
    gc = XCreateGC(dpy, pixmap, 0, NULL);
    if (plane)
        status = XCopyPlane(dpy, win, pixmap, gc, 0, 0, 16, 16, 0, 0, 1);
    else
        status = XCopyArea(dpy, win, pixmap, gc, 0, 0, 16, 16, 0, 0);
    XSync(dpy, False);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, pixmap);

    if (lastError)
        return lastError;
    return status ? 0 : -1;
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
readShm(Display *dpy, Window win, int x, int y, int format)
{
    Screen *screen = DefaultScreenOfDisplay(dpy);
    XShmSegmentInfo shminfo;
    XImage *image;
    size_t size;

    memset(&shminfo, 0, sizeof(shminfo));
    image = XShmCreateImage(dpy, DefaultVisualOfScreen(screen),
                            format == ZPixmap ?
                            (unsigned) DefaultDepthOfScreen(screen) : 1,
                            format, NULL, &shminfo, 16, 16);
    if (!image)
        return NULL;

    /* XYPixmap planes need far more room than the image line size, and the
     * server checks the segment against the request before it reads. */
    size = 64 * 1024;
    shminfo.shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (shminfo.shmid < 0)
        return NULL;

    shminfo.shmaddr = image->data = shmat(shminfo.shmid, NULL, 0);
    shminfo.readOnly = False;
    if (shminfo.shmaddr == (char *) -1 || !XShmAttach(dpy, &shminfo))
        return NULL;

    if (!XShmGetImage(dpy, win, image, x, y, AllPlanes)) {
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

    /* The window manager is exempt from the privacy view: take the selection
     * over and read what a plain client may not read. */
    if (argc > 2 && !strcmp(argv[1], "wmgetimage")) {
        char name[32];
        Atom wm;
        Window win = (Window) strtoul(argv[2], NULL, 0);

        snprintf(name, sizeof(name), "WM_S%d", DefaultScreen(dpy));
        wm = XInternAtom(dpy, name, False);
        XSetSelectionOwner(dpy, wm, root, CurrentTime);
        XSync(dpy, False);
        if (XGetSelectionOwner(dpy, wm) != root) {
            fprintf(stderr, "probe: cannot become the window manager\n");
            return 2;
        }
        return report(readWindow(dpy, win, ZPixmap));
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

        return report(readWindow(dpy, win, format));
    }

    if (argc > 1 && !strcmp(argv[1], "own")) {
        int format = (argc > 2 && !strcmp(argv[2], "xy")) ? XYPixmap : ZPixmap;
        Window win = XCreateSimpleWindow(dpy, root, 0, 0, 200, 200, 0, 0, 0);

        XMapWindow(dpy, win);
        XSync(dpy, False);
        usleep(200000);
        return report(readWindow(dpy, win, format));
    }

    if (argc > 1 && (!strcmp(argv[1], "copyarea") ||
                     !strcmp(argv[1], "copyplane"))) {
        int plane = !strcmp(argv[1], "copyplane");
        Window win;

        if (argc < 3) {
            fprintf(stderr, "probe: %s needs a source drawable\n", argv[1]);
            return 2;
        }
        if (!strcmp(argv[2], "root"))
            win = root;
        else if (!strcmp(argv[2], "own")) {
            win = XCreateSimpleWindow(dpy, root, 0, 0, 200, 200, 0, 0, 0);
            XMapWindow(dpy, win);
            XSync(dpy, False);
            usleep(200000);
        }
        else
            win = (Window) strtoul(argv[2], NULL, 0);

        return report(copyWindow(dpy, win, plane));
    }

    if (argc > 1 && (!strcmp(argv[1], "screen") || !strcmp(argv[1], "shm"))) {
        int shm = !strcmp(argv[1], "shm");
        int format = (argc > 4 && !strcmp(argv[4], "xy")) ? XYPixmap : ZPixmap;
        int x = argc > 2 ? atoi(argv[2]) : 600;
        int y = argc > 3 ? atoi(argv[3]) : 400;
        XImage *image;

        if (!paintRoot(dpy, root)) {
            fprintf(stderr, "probe: cannot paint the root window\n");
            return 2;
        }

        lastError = 0;
        image = shm ? readShm(dpy, root, x, y, format)
            : XGetImage(dpy, root, x, y, 16, 16, AllPlanes, format);
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

    if (argc > 1 && !strcmp(argv[1], "geom")) {
        Window child;
        unsigned int width, height, border, depth;
        int x, y;

        if (!XGetGeometry(dpy, root, &child, &x, &y, &width, &height,
                          &border, &depth)) {
            fprintf(stderr, "probe: cannot read the root geometry\n");
            return 2;
        }
        printf("%ux%u+%d+%d\n", width, height, x, y);
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

    fprintf(stderr, "usage: probe wm | wmgetimage W | prop V | getimage W|root [xy] |"
            " own [xy] | screen [X Y [xy]] | shm [X Y [xy]] | copyarea S |"
            " copyplane S | geom | attrs | setnet W H | getnet |"
            " setwork X Y W H | getwork\n");
    return 2;
}
