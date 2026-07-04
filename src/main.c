#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#define MAX_TEXT 8192
#define PADDING 6
#define LINE_SPACING 3
#define BORDER 3

struct {
    bool sticky;
    bool above;
    bool override_redirect;
    bool dialog;
    bool draw_box;
    bool focusable;
#ifndef NO_VERBOSE
    bool verbose;
#endif
} flags;

struct {
    unsigned short font_size;
    bool have_alpha;
    int depth;
} config;

typedef struct {
    Display *dpy;
    Window win;
    char *text;
    unsigned short x, y;
    unsigned short width, height;
    unsigned short text_width, text_height;
    XftFont *xft_font;
    XftColor xft_fg, xft_bg;
    XftDraw *xft_draw;
    unsigned long fg_pixel, bg_pixel, border_pixel;
    bool dragging;
    unsigned short drag_x, drag_y;
    Atom wm_delete_window;
    Visual *visual;
    Colormap cmap;
} SNote;

#ifndef NO_VERBOSE
void verbose_printf(const char *format, ...) {
    if (!flags.verbose) return;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}
#endif

XrmDatabase init_xresources(Display *dpy) {
    char *resource_string = XResourceManagerString(dpy);
    if (resource_string) {
#ifndef NO_VERBOSE
        verbose_printf("📚 Loading Xresources from server\n");
#endif
        return XrmGetStringDatabase(resource_string);
    }

    const char *home = getenv("HOME");
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.Xresources", home);
#ifndef NO_VERBOSE
        verbose_printf("📚 Loading Xresources from %s\n", path);
#endif

        FILE *fp = fopen(path, "r");
        if (fp) {
            fclose(fp);
            return XrmGetFileDatabase(path);
        }
    }

#ifndef NO_VERBOSE
    verbose_printf("⚠️ No Xresources found\n");
#endif
    return NULL;
}

unsigned long get_xcolor(Display *dpy, XrmDatabase db, const char *name, unsigned long fallback) {
    XrmValue value;
    char *type;

#ifndef NO_VERBOSE
    verbose_printf("🔍 Looking for color: %s\n", name);
#endif

    if (db) {
        char wildcard[256];
        snprintf(wildcard, sizeof(wildcard), "*.%s", name);

        if (XrmGetResource(db, wildcard, wildcard, &type, &value)) {
#ifndef NO_VERBOSE
            verbose_printf("   ✅ Found: %s = %s\n", wildcard, value.addr);
#endif

            XColor color;
            Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
            if (XParseColor(dpy, cmap, value.addr, &color)) {
                XAllocColor(dpy, cmap, &color);
#ifndef NO_VERBOSE
                verbose_printf("   ✅ Parsed color: pixel=%lu\n", color.pixel);
#endif
                return color.pixel;
            }
        }

        if (XrmGetResource(db, name, name, &type, &value)) {
#ifndef NO_VERBOSE
            verbose_printf("   ✅ Found: %s = %s\n", name, value.addr);
#endif

            XColor color;
            Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
            if (XParseColor(dpy, cmap, value.addr, &color)) {
                XAllocColor(dpy, cmap, &color);
#ifndef NO_VERBOSE
                verbose_printf("   ✅ Parsed color: pixel=%lu\n", color.pixel);
#endif
                return color.pixel;
            }
        }
    }

#ifndef NO_VERBOSE
    verbose_printf("   ⚠️ Using fallback: %lu\n", fallback);
#endif
    return fallback;
}

int get_font_face(XrmDatabase db, char *buffer, size_t bufsize) {
    XrmValue value;
    char *type;

    if (!buffer || bufsize == 0) return 0;

    buffer[0] = '\0';

    if (db) {
        if (XrmGetResource(db, "*.faceName", "*.faceName", &type, &value)) {
            strncpy(buffer, value.addr, bufsize - 1);
            buffer[bufsize - 1] = '\0';
#ifndef NO_VERBOSE
            verbose_printf("📝 Font face from Xresources: %s\n", buffer);
#endif
            return 1;
        }
        if (XrmGetResource(db, "faceName", "faceName", &type, &value)) {
            strncpy(buffer, value.addr, bufsize - 1);
            buffer[bufsize - 1] = '\0';
#ifndef NO_VERBOSE
            verbose_printf("📝 Font face from Xresources: %s\n", buffer);
#endif
            return 1;
        }
    }

#ifndef NO_VERBOSE
    verbose_printf("📝 No font face in Xresources\n");
#endif
    return 0;
}

int get_font_size_from_xresources(XrmDatabase db) {
    XrmValue value;
    char *type;

    if (db) {
        if (XrmGetResource(db, "*.fontSize", "*.fontSize", &type, &value)) {
            int size = atoi(value.addr);
#ifndef NO_VERBOSE
            verbose_printf("📐 Font size from Xresources: %d\n", size);
#endif
            return size;
        }
        if (XrmGetResource(db, "fontSize", "fontSize", &type, &value)) {
            int size = atoi(value.addr);
#ifndef NO_VERBOSE
            verbose_printf("📐 Font size from Xresources: %d\n", size);
#endif
            return size;
        }
    }

#ifndef NO_VERBOSE
    verbose_printf("📐 Using default font size: 20\n");
#endif
    return 20;
}


void measure_text(SNote *note) {
    if (!note || !note->text || strlen(note->text) == 0) return;

    char *text_copy = strdup(note->text);
    if (!text_copy) return;

    char *rest = text_copy;
    char *line = strsep(&rest, "\n");

    XGlyphInfo extents;
    int max_width = 0, total_height = 0;

    while (line) {
        XftTextExtentsUtf8(note->dpy, note->xft_font,
                           (XftChar8*)line, strlen(line), &extents);
        if (extents.xOff > max_width) max_width = extents.xOff;
        total_height += note->xft_font->height + LINE_SPACING;
        line = strsep(&rest, "\n");
    }
    free(text_copy);

    note->text_width = max_width;
    note->text_height = total_height - LINE_SPACING;
    note->width = max_width + PADDING * 2;
    note->height = total_height + PADDING * 2;
}

void apply_window_states(Display *dpy, Window win) {
    Atom states[10];
    int count = 0;
    Atom sticky_atom = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
    Atom above_atom = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);

    if (flags.sticky) {
        states[count++] = sticky_atom;
#ifndef NO_VERBOSE
        verbose_printf("📌 Adding STICKY state\n");
#endif
    }
    if (flags.above) {
        states[count++] = above_atom;
#ifndef NO_VERBOSE
        verbose_printf("📌 Adding ABOVE state\n");
#endif
    }

    if (count > 0) {
        XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_STATE", False),
                        XA_ATOM, 32, PropModeReplace, (unsigned char*)states, count);
    } else {
        XDeleteProperty(dpy, win, XInternAtom(dpy, "_NET_WM_STATE", False));
    }
}

void apply_window_type(Display *dpy, Window win) {
    Atom window_type = XInternAtom(dpy,
                                   flags.dialog ? "_NET_WM_WINDOW_TYPE_DIALOG" : "_NET_WM_WINDOW_TYPE_TOOL", False);
#ifndef NO_VERBOSE
    verbose_printf("📌 Setting window type: %s\n", flags.dialog ? "DIALOG" : "TOOL");
#endif
    XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace, (unsigned char*)&window_type, 1);
}

SNote *create_note(Display *dpy, XrmDatabase db, const char *text, int x, int y) {
    if (!dpy || !text) {
        fprintf(stderr, "❌ Invalid parameters for create_note\n");
        return NULL;
    }

    SNote *note = calloc(1, sizeof(SNote));
    if (!note) {
        fprintf(stderr, "❌ Failed to allocate memory for note\n");
        return NULL;
    }

    note->dpy = dpy;
    note->text = strdup(text);
    if (!note->text) {
        fprintf(stderr, "❌ Failed to duplicate text\n");
        free(note);
        return NULL;
    }
    note->x = x;
    note->y = y;
    note->dragging = 0;
    note->visual = NULL;

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    XVisualInfo vinfo;

    // for ARGB transparency
    if (XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo)) {
#ifndef NO_VERBOSE
        verbose_printf("✅ Found 32-bit TrueColor visual (depth=%d, class=%d)\n", vinfo.depth, vinfo.class);
#endif
        note->visual = vinfo.visual;
        note->cmap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);
        config.depth = vinfo.depth;
        config.have_alpha = 1;
    } else {
#ifndef NO_VERBOSE
        verbose_printf("⚠️ No 32-bit visual found, using default\n");
#endif
        note->visual = DefaultVisual(dpy, screen);
        note->cmap = DefaultColormap(dpy, screen);
        config.depth = DefaultDepth(dpy, screen);
        config.have_alpha = 0;
    }

    note->fg_pixel = get_xcolor(dpy, db, "foreground", BlackPixel(dpy, screen));
    note->bg_pixel = get_xcolor(dpy, db, "background", WhitePixel(dpy, screen));
    note->border_pixel = get_xcolor(dpy, db, "color7", BlackPixel(dpy, screen));

#ifndef NO_VERBOSE
    verbose_printf("🎨 Colors: fg=%lu, bg=%lu, border=%lu\n",
                   note->fg_pixel, note->bg_pixel, note->border_pixel);
#endif

    char font_face_buf[256] = {0};
    const char *font_face = NULL;
    char font_name[512];

    if (get_font_face(db, font_face_buf, sizeof(font_face_buf))) {
        font_face = font_face_buf;
    }

    if (font_face) {
        snprintf(font_name, sizeof(font_name), "%s:size=%d", font_face, config.font_size);
    } else {
        snprintf(font_name, sizeof(font_name), "sans:size=%d", config.font_size);
    }

#ifndef NO_VERBOSE
    verbose_printf("🔤 Creating Xft font: %s\n", font_name);
#endif
    note->xft_font = XftFontOpenName(dpy, screen, font_name);

    if (!note->xft_font) {
        static const char *fallbacks[] = {"DejaVu Sans", "Liberation Sans", "Arial", "Helvetica"};
        for (int i = 0; i < 4; i++) {
            snprintf(font_name, sizeof(font_name), "%s:size=%d", fallbacks[i], config.font_size);
#ifndef NO_VERBOSE
            verbose_printf("🔤 Trying fallback: %s\n", font_name);
#endif
            note->xft_font = XftFontOpenName(dpy, screen, font_name);
            if (note->xft_font) break;
        }
    }

    if (!note->xft_font) {
        fprintf(stderr, "❌ Cannot load any font\n");
        free(note->text);
        free(note);
        return NULL;
    }

#ifndef NO_VERBOSE
    verbose_printf("✅ Loaded font (size %d)\n", config.font_size);
#endif

    measure_text(note);

    XSetWindowAttributes attrs;
    attrs.background_pixmap = None;
    attrs.border_pixel = flags.draw_box ? note->border_pixel : 0;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | KeyPressMask | StructureNotifyMask |
                       PropertyChangeMask;
    attrs.colormap = note->cmap;
    attrs.save_under = 1;  // Essential for floating windows!

    unsigned long mask = CWBackPixmap | CWBorderPixel | CWEventMask |
                         CWColormap | CWSaveUnder | CWBackingPixel;

    if (config.have_alpha && config.depth == 32) {
        attrs.background_pixel = 0x00000000;
#ifndef NO_VERBOSE
        verbose_printf("🖼️ Using depth 32 with alpha transparency\n");
#endif
    } else {
        attrs.background_pixel = flags.draw_box ? note->bg_pixel : BlackPixel(dpy, screen);
#ifndef NO_VERBOSE
        verbose_printf("🖼️ Using default depth %d (no alpha)\n", config.depth);
#endif
    }

    if (flags.override_redirect) {
        attrs.override_redirect = True;
    }

    note->win = XCreateWindow(dpy, root, x, y, note->width, note->height,
                              flags.draw_box ? 2 : 0, config.depth, InputOutput, note->visual,
                              mask, &attrs);

    if (!note->win) {
        fprintf(stderr, "❌ Failed to create window\n");
        XftFontClose(dpy, note->xft_font);
        free(note->text);
        free(note);
        return NULL;
    }

#ifndef NO_VERBOSE
    verbose_printf("✅ Window created successfully (width=%d, height=%d)\n", note->width, note->height);
#endif

    XStoreName(dpy, note->win, "snote");

    apply_window_type(dpy, note->win);
    apply_window_states(dpy, note->win);

    // ── Size Hints (non-resizable) ──
    XSizeHints *size_hints = XAllocSizeHints();
    if (size_hints) {
        size_hints->flags = PMinSize | PMaxSize | PBaseSize;
        size_hints->min_width = note->width;
        size_hints->max_width = note->width;
        size_hints->min_height = note->height;
        size_hints->max_height = note->height;
        size_hints->base_width = note->width;
        size_hints->base_height = note->height;
        XSetWMNormalHints(dpy, note->win, size_hints);
        XFree(size_hints);
    }

    XWMHints wm_hints;
    wm_hints.flags = InputHint;
    wm_hints.input = flags.focusable ? True : False;
    XSetWMHints(dpy, note->win, &wm_hints);

    note->xft_draw = XftDrawCreate(dpy, note->win, note->visual, note->cmap);

    if (!note->xft_draw) {
        fprintf(stderr, "❌ Cannot create XftDraw\n");
        XDestroyWindow(dpy, note->win);
        XftFontClose(dpy, note->xft_font);
        free(note->text);
        free(note);
        return NULL;
    }

    XColor xcolor;
    XRenderColor render_color;

    xcolor.pixel = note->fg_pixel;
    XQueryColor(dpy, note->cmap, &xcolor);
    render_color.red = xcolor.red;
    render_color.green = xcolor.green;
    render_color.blue = xcolor.blue;
    render_color.alpha = 0xFFFF;
    XftColorAllocValue(dpy, note->visual, note->cmap, &render_color, &note->xft_fg);

    if (flags.draw_box) {
        xcolor.pixel = note->bg_pixel;
        XQueryColor(dpy, note->cmap, &xcolor);
        render_color.red = xcolor.red;
        render_color.green = xcolor.green;
        render_color.blue = xcolor.blue;
        render_color.alpha = 0xFFFF;
        XftColorAllocValue(dpy, note->visual, note->cmap, &render_color, &note->xft_bg);
    }

    note->wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, note->win, &note->wm_delete_window, 1);

    XMapWindow(dpy, note->win);
    XFlush(dpy);

#ifndef NO_VERBOSE
    verbose_printf("✅ Window mapped\n");
#endif

    return note;
}

void draw_note(SNote *note) {
    if (!note || !note->xft_draw) return;

#ifndef NO_VERBOSE
    verbose_printf("🎨 Drawing note (box=%d, w=%d, h=%d)\n",
                   flags.draw_box, note->width, note->height);
#endif

    XWindowAttributes attrs;
    XGetWindowAttributes(note->dpy, note->win, &attrs);

    // prevents artifacts if letters are smaller than minimum window size
    unsigned int width = attrs.width;
    unsigned int height = attrs.height;

    if (flags.draw_box) {
        GC gc = XCreateGC(note->dpy, note->win, 0, NULL);
        if (gc) {
            // Draw border
            XSetForeground(note->dpy, gc, note->border_pixel);
            XFillRectangle(note->dpy, note->win, gc, 0, 0,
                           width, height);
#ifndef NO_VERBOSE
            verbose_printf("🎨 Drew border with pixel %lu\n", note->border_pixel);
#endif

            // Fill background
            XSetForeground(note->dpy, gc, note->bg_pixel);
            XFillRectangle(note->dpy, note->win, gc, BORDER, BORDER,
                           width - BORDER * 2 - 1, height - BORDER * 2 - 1);
#ifndef NO_VERBOSE
            verbose_printf("🎨 Filled background with pixel %lu\n", note->bg_pixel);
#endif

            XFreeGC(note->dpy, gc);
        }
    } else {
        if (config.have_alpha && config.depth == 32) {
            // Clear with transparent pixel
            GC gc = XCreateGC(note->dpy, note->win, 0, NULL);
            if (gc) {
                XSetForeground(note->dpy, gc, 0x00000000);
                XFillRectangle(note->dpy, note->win, gc, 0, 0,
                               width, height);
                XFreeGC(note->dpy, gc);
            }
        }
    }

    if (!note->text || strlen(note->text) == 0) return;

    char *text_copy = strdup(note->text);
    if (!text_copy) return;

    char *rest = text_copy;
    char *line = strsep(&rest, "\n");
    int y = PADDING + note->xft_font->ascent;

    while (line && y < note->height - PADDING) {
        XftDrawStringUtf8(note->xft_draw, &note->xft_fg, note->xft_font,
                          PADDING, y, (XftChar8*)line, strlen(line));
#ifndef NO_VERBOSE
        verbose_printf("🎨 Drew text: '%s' at y=%d, +%d\n", line, y, note->xft_font->height);
#endif
        y +=  note->xft_font->height + LINE_SPACING;
        line = strsep(&rest, "\n");
    }
    free(text_copy);

#ifndef NO_VERBOSE
    verbose_printf("Actual window size: %dx%d at (%d,%d)\n", width, height, attrs.x, attrs.y);
#endif
}

void move_window(SNote *note, int x, int y) {
    if (!note) return;
    note->x = x;
    note->y = y;
    XMoveWindow(note->dpy, note->win, x, y);

    XFlush(note->dpy);
}

void print_help(const char *prog) {
    printf("snote - Suckless floating text widget\n\n");
    printf("Usage: %s [OPTIONS] \"text\"\n\n", prog);
    printf("Options:\n");
    printf("  -x <pixels>      X position (default: 100)\n");
    printf("  -y <pixels>      Y position (default: 100)\n");
    printf("  -s <size>        Font size (default: from Xresources or 20)\n");
    printf("  -S               Sticky (stays on workspace via _NET_WM_STATE_STICKY)\n");
    printf("  -F               Focusable (default: not focusable)\n");
    printf("  -a               Above all windows (_NET_WM_STATE_ABOVE)\n");
    printf("  -o               Override Redirect (bypass WM)\n");
    printf("  -b               Draw box border (default: transparent, text only)\n");
    printf("  -d               Dialog window type (default: TOOL)\n");
#ifndef NO_VERBOSE
    printf("  -v               Enable verbose/debug output\n");
#endif
    printf("  -h, --help       Show this help\n\n");
    printf("Examples:\n");
    printf("  %s -x 200 -y 300 -s 20 \"Hello\\nWorld\"\n", prog);
    printf("  %s -S -a -b \"Important Note\"\n", prog);
    printf("  %s -s 30 \"$(cat note.txt)\"\n\n", prog);
    printf("Xresources settings:\n");
    printf("  foreground      - Text color\n");
    printf("  background      - Box background color (with -b)\n");
    printf("  color7          - Border color (with -b)\n");
    printf("  faceName        - Font face (e.g., \"ComicShannsMono Nerd Font\")\n");
    printf("  fontSize        - Default font size (optional)\n");
}

void destroy_note(SNote *note) {
    if (!note) return;

    if (note->xft_draw) {
        XftDrawDestroy(note->xft_draw);
        note->xft_draw = NULL;
    }

    if (note->xft_font) {
        XftFontClose(note->dpy, note->xft_font);
        note->xft_font = NULL;
    }

    if (note->win) {
        XDestroyWindow(note->dpy, note->win);
        note->win = 0;
    }

    // Free Xft colors
    if (note->visual && note->cmap) {
        XftColorFree(note->dpy, note->visual, note->cmap, &note->xft_fg);
        XftColorFree(note->dpy, note->visual, note->cmap, &note->xft_bg);
    }

    if (note->text) {
        free(note->text);
        note->text = NULL;
    }

    free(note);
}

int main(int argc, char *argv[]) {
    unsigned short x = 100, y = 100;
    const char *text = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            x = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            y = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config.font_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-S") == 0) {
            flags.sticky = 1;
        } else if (strcmp(argv[i], "-F") == 0) {
            flags.focusable = 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            flags.above = 1;
        } else if (strcmp(argv[i], "-o") == 0) {
            flags.override_redirect = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            flags.dialog = 1;
        } else if (strcmp(argv[i], "-b") == 0) {
            flags.draw_box = 1;
#ifndef NO_VERBOSE
        } else if (strcmp(argv[i], "-v") == 0) {
            flags.verbose = 1;
#endif
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else {
            text = argv[i];
        }
    }

    if (!text) {
        fprintf(stderr, "Error: No text provided\n\n");
        print_help(argv[0]);
        return 1;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

#ifndef NO_VERBOSE
    verbose_printf("📝 Creating note: \"%s\"\n", text);
    verbose_printf("   Position: %d, %d\n", x, y);
    verbose_printf("   Sticky: %s, Above: %s, Focusable: %s, Override: %s, Dialog: %s, Box: %s\n",
                   flags.sticky ? "yes" : "no", flags.above ? "yes" : "no",
                   flags.focusable ? "yes" : "no",
                   flags.override_redirect ? "yes" : "no", flags.dialog ? "yes" : "no",
                   flags.draw_box ? "yes" : "no");
#endif

    XrmDatabase db = init_xresources(dpy);
    if (config.font_size == 0) {
        config.font_size = get_font_size_from_xresources(db);
    }

    SNote *note = create_note(dpy, db, text, x, y);

    // ealrly cleanup for resources not used after note inited
    XrmDestroyDatabase(db);

    if (!note) {
        fprintf(stderr, "❌ Failed to create note\n");
        XCloseDisplay(dpy);
        return 1;
    }

    XEvent ev;
    int running = 1;
    while (running) {
        XNextEvent(dpy, &ev);

        switch (ev.type) {
        case Expose:
            draw_note(note);
            break;

        case ButtonPress:
            if (ev.xbutton.button == Button1) {
                note->dragging = 1;
                note->drag_x = ev.xbutton.x_root - note->x;
                note->drag_y = ev.xbutton.y_root - note->y;
            } else if (ev.xbutton.button == Button3) {
                running = 0;
            }
            break;

        case ButtonRelease:
            if (ev.xbutton.button == Button1) {
                note->dragging = 0;
            }
            break;

        case MotionNotify:
            if (note->dragging) {
                int new_x = ev.xmotion.x_root - note->drag_x;
                int new_y = ev.xmotion.y_root - note->drag_y;
                move_window(note, new_x, new_y);
            }
            break;

        case KeyPress: {
            KeySym keysym = XLookupKeysym(&ev.xkey, 0);
            if (keysym == XK_Escape || keysym == XK_q) {
                running = 0;
            }
            break;
        }

        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == note->wm_delete_window) {
                running = 0;
            }
            break;

        case ConfigureNotify:
            note->x = ev.xconfigure.x;
            note->y = ev.xconfigure.y;
            break;

        default:
            break;
        }
    }

    // Cleanup
    destroy_note(note);
    XCloseDisplay(dpy);

    return 0;
}
