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

#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "windowlab.h"

static void quit_nicely(void);

void err(const char *fmt, ...)
{
	va_list argp;

	fprintf(stderr, "windowlab: ");
	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	fprintf(stderr, "\n");
}

void fork_exec(char *cmd)
{
	pid_t pid = fork();

	switch (pid)
	{
		case 0:
			setsid();
			execlp("/bin/sh", "sh", "-c", cmd, NULL);
			err("exec failed, cleaning up child");
			exit(1);
		case -1:
			err("can't fork");
	}
}

void sig_handler(int signal)
{
	switch (signal)
	{
		case SIGINT:
		case SIGTERM:
		case SIGHUP:
			quit_nicely();
			break;
		case SIGCHLD:
			wait(NULL);
			break;
	}
}

int handle_xerror(Display *dpy, XErrorEvent *e)
{
	Client *c = find_client(e->resourceid, WINDOW);

	if (e->error_code == BadAccess && e->resourceid == root)
	{
		err("root window unavailable (maybe another wm is running?)");
		exit(1);
	}
	else
	{
		char msg[255];
		XGetErrorText(dpy, e->error_code, msg, sizeof msg);
		err("X error (%#lx): %s", e->resourceid, msg);
	}

	if (c)
	{
		remove_client(c, WITHDRAW);
	}
	return 0;
}

/* Ick. Argh. You didn't see this function. */

int ignore_xerror(Display *dpy, XErrorEvent *e)
{
	return 0;
}

/* Currently, only send_wm_delete uses this one... */

int send_xmessage(Window w, Atom a, long x)
{
	XEvent e;

	e.type = ClientMessage;
	e.xclient.window = w;
	e.xclient.message_type = a;
	e.xclient.format = 32;
	e.xclient.data.l[0] = x;
	e.xclient.data.l[1] = CurrentTime;

	return XSendEvent(dpy, w, False, NoEventMask, &e);
}

void get_mouse_position(int *x, int *y)
{
	Window mouse_root, mouse_win;
	int win_x, win_y;
	unsigned int mask;

	XQueryPointer(dpy, root, &mouse_root, &mouse_win,
		x, y, &win_x, &win_y, &mask);
}

void fix_position(Client *c)
{
	unsigned int xmax = DisplayWidth(dpy, screen);
	unsigned int ymax = DisplayHeight(dpy, screen);
	c->y += BARHEIGHT();

	if (c->width < MINWINWIDTH)
	{
		c->width = MINWINWIDTH;
	}
	if (c->height < MINWINHEIGHT)
	{
		c->height = MINWINHEIGHT;
	}
	
	if (c->width > xmax)
	{
		c->width = xmax;
	}
	if (c->height + (BARHEIGHT() * 2) > ymax)
	{
		c->height = ymax - (BARHEIGHT() * 2);
	}

	if (c->x < 0)
	{
		c->x = 0;
	}
	if (c->y <= (BARHEIGHT() + BW(c)))
	{
		c->y = (BARHEIGHT() + BW(c));
	}

	if (c->x + c->width + BW(c) >= xmax)
	{
		c->x = xmax - c->width;
	}
	if (c->y + c->height + BARHEIGHT() + BW(c) >= ymax)
	{
		c->y = (ymax - c->height) - BARHEIGHT();
	}

	c->x -= BW(c);
	c->y -= BW(c);
}

void refix_position(Client *c, XConfigureRequestEvent *e)
{
	unsigned int xmax = DisplayWidth(dpy, screen);
	unsigned int ymax = DisplayHeight(dpy, screen);

	if (c->width < MINWINWIDTH - BW(c))
	{
		c->width = MINWINWIDTH - BW(c);
		e->value_mask |= CWWidth;
	}
	if (c->height < MINWINHEIGHT - BW(c))
	{
		c->height = MINWINHEIGHT - BW(c);
		e->value_mask |= CWHeight;
	}
	
	if (c->width + BW(c) > xmax)
	{
		c->width = xmax;
		e->value_mask |= CWWidth;
	}
	if (c->height + (BARHEIGHT() * 2) + BW(c) > ymax)
	{
		c->height = ymax - (BARHEIGHT() * 2);
		e->value_mask |= CWHeight;
	}

	if (c->x + BW(c) < 0)
	{
		c->x = 0 - BW(c);
		e->value_mask |= CWX;
	}
	if (c->y <= BARHEIGHT() * 2)
	{
		c->y = BARHEIGHT() * 2;
		e->value_mask |= CWY;
	}

	if (c->x + c->width >= xmax)
	{
		c->x = xmax - c->width - BW(c);
		e->value_mask |= CWX;
	}
	// note that this next bit differs from fix_position() because here we ensure that the titlebar is visible as opposed to the whole window
	if (c->y + BARHEIGHT() >= ymax)
	{
		c->y = ymax - BARHEIGHT() - BW(c);
		e->value_mask |= CWY;
	}
}

#ifdef DEBUG

/* Bleh, stupid macro names. I'm not feeling creative today. */

#define SHOW_EV(name, memb) \
	case name: \
		s = #name; \
		w = e.memb.window; \
		break;
#define SHOW(name) \
	case name: \
		return #name;

void show_event(XEvent e)
{
	char *s, buf[20];
	Window w;
	Client *c;

	switch (e.type)
	{
		SHOW_EV(ButtonPress, xbutton)
		SHOW_EV(ButtonRelease, xbutton)
		SHOW_EV(ClientMessage, xclient)
		SHOW_EV(ColormapNotify, xcolormap)
		SHOW_EV(ConfigureNotify, xconfigure)
		SHOW_EV(ConfigureRequest, xconfigurerequest)
		SHOW_EV(CreateNotify, xcreatewindow)
		SHOW_EV(DestroyNotify, xdestroywindow)
		SHOW_EV(EnterNotify, xcrossing)
		SHOW_EV(Expose, xexpose)
		SHOW_EV(MapNotify, xmap)
		SHOW_EV(MapRequest, xmaprequest)
		SHOW_EV(MappingNotify, xmapping)
		SHOW_EV(MotionNotify, xmotion)
		SHOW_EV(PropertyNotify, xproperty)
		SHOW_EV(ReparentNotify, xreparent)
		SHOW_EV(ResizeRequest, xresizerequest)
		SHOW_EV(UnmapNotify, xunmap)
		default:
			if (shape && e.type == shape_event)
			{
				s = "ShapeNotify";
				w = ((XShapeEvent *)&e)->window;
			}
			else
			{
				s = "unknown event";
				w = None;
			}
			break;
	}

	c = find_client(w, WINDOW);
	snprintf(buf, sizeof buf, c ? c->name : "(none)");
	err("%#-10lx: %-20s: %s", w, buf, s);
}

static const char *show_state(Client *c)
{
	switch (get_wm_state(c))
	{
		SHOW(WithdrawnState)
		SHOW(NormalState)
		SHOW(IconicState)
		default: return "unknown state";
	}
}

static const char *show_grav(Client *c)
{
	if (!c->size || !(c->size->flags & PWinGravity))
	{
		return "no grav (NW)";
	}

	switch (c->size->win_gravity)
	{
		SHOW(UnmapGravity)
		SHOW(NorthWestGravity)
		SHOW(NorthGravity)
		SHOW(NorthEastGravity)
		SHOW(WestGravity)
		SHOW(CenterGravity)
		SHOW(EastGravity)
		SHOW(SouthWestGravity)
		SHOW(SouthGravity)
		SHOW(SouthEastGravity)
		SHOW(StaticGravity)
		default: return "unknown grav";
	}
}

void dump(Client *c)
{
	if (c)
	{
		err("%s\n\t%s, %s, ignore %d\n"
			"\tframe %#lx, win %#lx, geom %dx%d+%d+%d",
			c->name, show_state(c), show_grav(c), c->ignore_unmap,
			c->frame, c->window, c->width, c->height, c->x, c->y);
	}
}

void dump_clients()
{
	Client *c = head_client;
	while (c != NULL)
	{
		dump(c);
		c = c->next;
	}
}
#endif

/* We use XQueryTree here to preserve the window stacking order,
 * since the order in our linked list is different. */

static void quit_nicely(void)
{
	unsigned int nwins, i;
	Window dummyw1, dummyw2, *wins;
	Client *c;

	free_menuitems();

	XQueryTree(dpy, root, &dummyw1, &dummyw2, &wins, &nwins);
	for (i = 0; i < nwins; i++)
	{
		c = find_client(wins[i], FRAME);
		if (c)
		{
			remove_client(c, REMAP);
		}
	}
	XFree(wins);

	XFreeFont(dpy, font);
#ifdef XFT
	XftFontClose(dpy, xftfont);
#endif
	XFreeCursor(dpy, move_curs);
	XFreeCursor(dpy, resizestart_curs);
	XFreeCursor(dpy, resizeend_curs);
	XFreeGC(dpy, border_gc);
	XFreeGC(dpy, string_gc);

	XInstallColormap(dpy, DefaultColormap(dpy, screen));
	XSetInputFocus(dpy, PointerRoot, RevertToNone, CurrentTime);

	XCloseDisplay(dpy);
	exit(0);
}
