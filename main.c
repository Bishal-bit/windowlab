/* WindowLab - an X11 window manager
 * Copyright (c) 2001-2003 Nick Gravgaard
 * me at nickgravgaard.com
 * http://nickgravgaard.com/windowlab/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <string.h>
#include <signal.h>
#include <X11/cursorfont.h>
#include "windowlab.h"

Display *dpy;
Window root;
int screen;
XFontStruct *font;
#ifdef XFT
XftFont *xftfont;
XftColor xft_detail;
#endif
GC string_gc, border_gc, text_gc, active_gc, depressed_gc, inactive_gc, menu_gc, selected_gc, empty_gc;
XColor border_col, text_col, active_col, depressed_col, inactive_col, menu_col, selected_col, empty_col;
Cursor move_curs, resizestart_curs, resizeend_curs;
Atom wm_state, wm_change_state, wm_protos, wm_delete, wm_cmapwins;
#ifdef MWM_HINTS
Atom mwm_hints;
#endif
Client *head_client, *last_focused_client, *fullscreen_client;
unsigned int in_taskbar = 0; // actually, we don't know yet
unsigned int showing_taskbar = 1;
Rect fs_prevdims;
char *opt_font = DEF_FONT;
char *opt_border = DEF_BORDER;
char *opt_text = DEF_TEXT;
char *opt_active = DEF_ACTIVE;
char *opt_inactive = DEF_INACTIVE;
char *opt_menu = DEF_MENU;
char *opt_selected = DEF_SELECTED;
char *opt_empty = DEF_EMPTY;
#ifdef SHAPE
Bool shape;
int shape_event;
#endif
unsigned int numlockmask = 0;

static void scan_wins(void);
static void setup_display(void);

int main(int argc, char **argv)
{
	int i;
	struct sigaction act;

#define OPT_STR(name, variable)	 \
	if (strcmp(argv[i], name) == 0 && i+1<argc) \
	{ \
		variable = argv[++i]; \
		continue; \
	}

	for (i = 1; i < argc; i++)
	{
		OPT_STR("-font", opt_font)
		OPT_STR("-border", opt_border)
		OPT_STR("-text", opt_text)
		OPT_STR("-active", opt_active)
		OPT_STR("-inactive", opt_inactive)
		OPT_STR("-menu", opt_menu)
		OPT_STR("-selected", opt_selected)
		OPT_STR("-empty", opt_empty)
		if (strcmp(argv[i], "-version") == 0)
		{
			printf("windowlab: version " VERSION "\n");
			exit(0);
		}
		// shouldn't get here; must be a bad option
		err("usage: windowlab [options]\n    options are: -font <font>, -border|-text|-active|-inactive|-menu|-selected|-empty <color>");
		return 2;
	}

	act.sa_handler = sig_handler;
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGCHLD, &act, NULL);

	setup_display();
	get_menuitems();
	head_client = 0;
	make_taskbar();
	scan_wins();
	do_event_loop();
	return 1; //just another brick in the -Wall
}

static void scan_wins(void)
{
	unsigned int nwins, i;
	Window dummyw1, dummyw2, *wins;
	XWindowAttributes attr;

	XQueryTree(dpy, root, &dummyw1, &dummyw2, &wins, &nwins);
	for (i = 0; i < nwins; i++)
	{
		XGetWindowAttributes(dpy, wins[i], &attr);
		if (!attr.override_redirect && attr.map_state == IsViewable)
		{
			make_new_client(wins[i]);
		}
	}
	XFree(wins);
}

static void setup_display(void)
{
	XColor dummyc;
	XGCValues gv;
	XSetWindowAttributes sattr;
	XModifierKeymap *modmap;
	unsigned int i, j;
#ifdef SHAPE
	int dummy;
#endif

	dpy = XOpenDisplay(NULL);

	if (!dpy)
	{
		err("can't open display! check your DISPLAY variable.");
		exit(1);
	}

	XSetErrorHandler(handle_xerror);
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	wm_state = XInternAtom(dpy, "WM_STATE", False);
	wm_change_state = XInternAtom(dpy, "WM_CHANGE_STATE", False);
	wm_protos = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wm_cmapwins = XInternAtom(dpy, "WM_COLORMAP_WINDOWS", False);
#ifdef MWM_HINTS
	mwm_hints = XInternAtom(dpy, _XA_MWM_HINTS, False);
#endif

	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_border, &border_col, &dummyc);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_text, &text_col, &dummyc);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_active, &active_col, &dummyc);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_inactive, &inactive_col, &dummyc);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_menu, &menu_col, &dummyc);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_selected, &selected_col, &dummyc);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), opt_empty, &empty_col, &dummyc);

	depressed_col.pixel = active_col.pixel;
	depressed_col.red = active_col.red - ACTIVE_SHADOW;
	depressed_col.green = active_col.green - ACTIVE_SHADOW;
	depressed_col.blue = active_col.blue - ACTIVE_SHADOW;
	depressed_col.red = depressed_col.red <= (USHRT_MAX - ACTIVE_SHADOW) ? depressed_col.red : 0;
	depressed_col.green = depressed_col.green <= (USHRT_MAX - ACTIVE_SHADOW) ? depressed_col.green : 0;
	depressed_col.blue = depressed_col.blue <= (USHRT_MAX - ACTIVE_SHADOW) ? depressed_col.blue : 0;
	XAllocColor(dpy, DefaultColormap(dpy, screen), &depressed_col);

	font = XLoadQueryFont(dpy, opt_font);
	if (!font)
	{
		err("font '%s' not found", opt_font);
		exit(1);
	}

#ifdef XFT
	xft_detail.color.red = text_col.red;
	xft_detail.color.green = text_col.green;
	xft_detail.color.blue = text_col.blue;
	xft_detail.color.alpha = 0xffff;
	xft_detail.pixel = text_col.pixel;

	xftfont = XftFontOpenXlfd(dpy, DefaultScreen(dpy), opt_font);
	if (!xftfont)
	{
		err("font '%s' not found", opt_font);
		exit(1);
	}
#endif

#ifdef SHAPE
	shape = XShapeQueryExtension(dpy, &shape_event, &dummy);
#endif

	move_curs = XCreateFontCursor(dpy, XC_fleur);
	resizestart_curs = XCreateFontCursor(dpy, XC_ul_angle);
	resizeend_curs = XCreateFontCursor(dpy, XC_lr_angle); //XC_bottom_right_corner

	/* find out which modifier is NumLock - we'll use this when grabbing
	 * every combination of modifiers we can think of */
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++) {
		for (j = 0; j < modmap->max_keypermod; j++) {
			if (modmap->modifiermap[i*modmap->max_keypermod+j] == XKeysymToKeycode(dpy, XK_Num_Lock)) {
				numlockmask = (1<<i);
#ifdef DEBUG
				fprintf(stderr, "setup_display() : XK_Num_Lock is (1<<0x%02x)\n", i);
#endif
			}
		}
	}
	XFree(modmap);

	gv.function = GXcopy;

	gv.foreground = border_col.pixel;
	gv.line_width = DEF_BORDERWIDTH;
	border_gc = XCreateGC(dpy, root, GCFunction|GCForeground|GCLineWidth, &gv);

	gv.foreground = text_col.pixel;
	gv.line_width = 1;
	gv.font = font->fid;
	text_gc = XCreateGC(dpy, root, GCFunction|GCForeground|GCFont, &gv);

	gv.foreground = active_col.pixel;
	active_gc = XCreateGC(dpy, root, GCFunction|GCForeground, &gv);

	gv.foreground = depressed_col.pixel;
	depressed_gc = XCreateGC(dpy, root, GCFunction|GCForeground, &gv);

	gv.foreground = inactive_col.pixel;
	inactive_gc = XCreateGC(dpy, root, GCFunction|GCForeground, &gv);

	gv.foreground = menu_col.pixel;
	menu_gc = XCreateGC(dpy, root, GCFunction|GCForeground, &gv);

	gv.foreground = selected_col.pixel;
	selected_gc = XCreateGC(dpy, root, GCFunction|GCForeground, &gv);

	gv.foreground = empty_col.pixel;
	empty_gc = XCreateGC(dpy, root, GCFunction|GCForeground, &gv);

	sattr.event_mask = ChildMask|ColormapChangeMask|ButtonMask;
	XChangeWindowAttributes(dpy, root, CWEventMask, &sattr);

	// change Mod1Mask to None to remove the need to hold down Alt
	grab_keysym(root, Mod1Mask, KEY_CYCLEPREV);
	grab_keysym(root, Mod1Mask, KEY_CYCLENEXT);
	grab_keysym(root, Mod1Mask, KEY_FULLSCREEN);
	grab_keysym(root, Mod1Mask, KEY_TOGGLEZ);
}
