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

#include "windowlab.h"

static void drag(Client *);
static void sweep(Client *);
static void recalc_sweep(Client *, int, int, Rect *);
static int get_incsize(Client *, int *, int *, Rect *, int);

void move(Client *c)
{
	drag(c);
}

void raise_lower(Client *c)
{
	static Client *topmost_client;
	if (c != topmost_client)
	{
		raise_win(c);
		topmost_client = c;
	}
	else
	{
		lower_win(c);
		topmost_client = NULL; // lazy but amiwm does similar
	}
}

void resize(Client *c)
{
	sweep(c);
}

void hide(Client *c)
{
	if (!c->ignore_unmap) c->ignore_unmap++;
	c->iconic = 1;
	XUnmapWindow(dpy, c->frame);
	XUnmapWindow(dpy, c->window);
	set_wm_state(c, IconicState);
}

void unhide(Client *c)
{
	if (c->ignore_unmap) c->ignore_unmap--;
	c->iconic = 0;
	XMapWindow(dpy, c->window);
	XMapRaised(dpy, c->frame);
	set_wm_state(c, NormalState);
}

void toggle_fullscreen(Client *c)
{
	unsigned int xoffset, yoffset, maxwinwidth, maxwinheight;
	if (c && !c->trans)
	{
		if (c == fullscreen_client) // reset to original size
		{
			c->x = fs_prevdims.x;
			c->y = fs_prevdims.y;
			c->width = fs_prevdims.width;
			c->height = fs_prevdims.height;
			XMoveResizeWindow(dpy, c->frame, c->x, c->y - BARHEIGHT(), c->width, c->height + BARHEIGHT());
			XMoveResizeWindow(dpy, c->window, 0, BARHEIGHT(), c->width, c->height);
			send_config(c);
			fullscreen_client = NULL;
			showing_taskbar = 1;
		}
		else // make fullscreen
		{
			xoffset = yoffset = 0;
			maxwinwidth = DisplayWidth(dpy, screen);
			maxwinheight = DisplayHeight(dpy, screen) - BARHEIGHT();
			if (fullscreen_client) // reset existing fullscreen window to original size
			{
				fullscreen_client->x = fs_prevdims.x;
				fullscreen_client->y = fs_prevdims.y;
				fullscreen_client->width = fs_prevdims.width;
				fullscreen_client->height = fs_prevdims.height;
				XMoveResizeWindow(dpy, fullscreen_client->frame, fullscreen_client->x, fullscreen_client->y - BARHEIGHT(), fullscreen_client->width, fullscreen_client->height + BARHEIGHT());
				XMoveResizeWindow(dpy, fullscreen_client->window, 0, BARHEIGHT(), fullscreen_client->width, fullscreen_client->height);
				send_config(fullscreen_client);
			}
			fs_prevdims.x = c->x;
			fs_prevdims.y = c->y;
			fs_prevdims.width = c->width;
			fs_prevdims.height = c->height;
			c->x = 0 - BORDERWIDTH(c);
			c->y = BARHEIGHT() - DEF_BORDERWIDTH;
			c->width = maxwinwidth;
			c->height = maxwinheight;
			if (c->size->flags & PMaxSize)
			{
				if (c->size->max_width < maxwinwidth)
				{
					c->width = c->size->max_width;
					xoffset = (maxwinwidth - c->width) / 2;
				}
				if (c->size->max_height < maxwinheight)
				{
					c->height = c->size->max_height;
					yoffset = (maxwinheight - c->height) / 2;
				}
			}
			XMoveResizeWindow(dpy, c->frame, c->x, c->y, maxwinwidth, maxwinheight);
			XMoveResizeWindow(dpy, c->window, xoffset, yoffset, c->width, c->height);
			send_config(c);
			fullscreen_client = c;
			showing_taskbar = in_taskbar;
		}
		redraw_taskbar();
	}
}

/* The name of this function is a bit misleading: if the client
 * doesn't listen to WM_DELETE then we just terminate it with extreme
 * prejudice. */

void send_wm_delete(Client *c)
{
	int i, n, found = 0;
	Atom *protocols;

	if (XGetWMProtocols(dpy, c->window, &protocols, &n))
	{
		for (i=0; i<n; i++)
		{
			if (protocols[i] == wm_delete)
			{
				found++;
			}
		}
		XFree(protocols);
	}
	if (found)
	{
		send_xmessage(c->window, wm_protos, wm_delete);
	}
	else
	{
		XKillClient(dpy, c->window);
	}
	if (c == last_focused_client)
	{
		last_focused_client = NULL;
	}
}

static void drag(Client *c)
{
	XEvent ev;
	int old_cx = c->x;
	int old_cy = c->y;
	int x1, y1, mousex, mousey, dw, dh;
	Client *exposed_c;
	Rect bound;
	Window constraint_win;
	XSetWindowAttributes pattr;

	dw = DisplayWidth(dpy, screen);
	dh = DisplayHeight(dpy, screen);
	get_mouse_position(&mousex, &mousey);

	bound.x = (mousex - c->x) - BORDERWIDTH(c);
	bound.width = (dw - bound.x - (c->width - bound.x)) + 1;
	bound.y = mousey - c->y;
	bound.height = (dh - bound.y - (c->height - bound.y)) + 1;
	bound.y += (BARHEIGHT() * 2) - DEF_BORDERWIDTH;
	bound.height += c->height - ((BARHEIGHT() * 2) - DEF_BORDERWIDTH);

	constraint_win = XCreateWindow(dpy, root, bound.x, bound.y, bound.width, bound.height, 0, CopyFromParent, InputOnly, CopyFromParent, 0, &pattr);
	XMapWindow(dpy, constraint_win);

	if (!(XGrabPointer(dpy, root, False, MouseMask, GrabModeAsync, GrabModeAsync, constraint_win, move_curs, CurrentTime) == GrabSuccess))
	{
		XDestroyWindow(dpy, constraint_win);
		return;
	}
	get_mouse_position(&x1, &y1);

	do
	{
		XMaskEvent(dpy, ExposureMask|MouseMask, &ev);
		switch (ev.type)
		{
			case Expose:
				exposed_c = find_client(ev.xexpose.window, FRAME);
				if (exposed_c)
				{
					redraw(exposed_c);
				}
				break;
			case MotionNotify:
				c->x = old_cx + (ev.xmotion.x - x1);
				c->y = old_cy + (ev.xmotion.y - y1);
				XMoveWindow(dpy, c->frame, c->x, c->y - BARHEIGHT());
				send_config(c);
				break;
		}
	}
	while (ev.type != ButtonRelease);
	ungrab();
	XDestroyWindow(dpy, constraint_win);
}

static void sweep(Client *c)
{
	XEvent ev;
	Client *exposed_c, *copy_focused;
	Rect newdims, bound;
	int mousex, mousey, dw, dh, minw, minh;
	Window constraint_win, resize_win, resizebar_win;
	XSetWindowAttributes pattr, resize_pattr, resizebar_pattr;

	dw = DisplayWidth(dpy, screen);
	dh = DisplayHeight(dpy, screen);
	get_mouse_position(&mousex, &mousey);

	bound.x = 1;
	bound.width = dw;
	bound.y = BARHEIGHT() + 1;
	bound.height = dh - BARHEIGHT();

	constraint_win = XCreateWindow(dpy, root, bound.x, bound.y, bound.width, bound.height, 0, CopyFromParent, InputOnly, CopyFromParent, 0, &pattr);
	XMapWindow(dpy, constraint_win);

	if (!(XGrabPointer(dpy, root, False, MouseMask, GrabModeAsync, GrabModeAsync, constraint_win, resizestart_curs, CurrentTime) == GrabSuccess))
	{
		XDestroyWindow(dpy, constraint_win);
		return;
	}

	do
	{
		XMaskEvent(dpy, MouseMask, &ev);
	}
	while (ev.type != ButtonPress);
	get_mouse_position(&newdims.x, &newdims.y);

	copy_focused = last_focused_client;
	last_focused_client = NULL;
	redraw(copy_focused);

	newdims.x -= (BORDERWIDTH(c) + 1);
	newdims.y -= (BORDERWIDTH(c) + 1);
	newdims.y += BARHEIGHT();

	minw = c->size->min_width > MINWINWIDTH ? c->size->min_width : MINWINWIDTH;
	minh = c->size->min_height > MINWINHEIGHT ? c->size->min_height : MINWINHEIGHT;

	// work around insane default minimum sizes
	if (minw < 0 || minw > (dw - 1))
	{
		minw = MINWINWIDTH;
	}
	if (minh < 0 || minh > (dh - 1))
	{
		minh = MINWINHEIGHT;
	}

	recalc_sweep(c, newdims.x + minw, newdims.y + minh, &newdims);

	if ((newdims.x + newdims.width) > dw)
	{
		newdims.x = ((dw - newdims.width) - 1) - BORDERWIDTH(c);
	}
	if ((newdims.y + newdims.height) > dh)
	{
		newdims.y = ((dh - newdims.height) - 1) - BORDERWIDTH(c);
	}

	bound.x = (newdims.x + newdims.width);
	bound.y = (newdims.y + newdims.height);
	bound.width = (dw - bound.x);
	bound.height = (dh - bound.y);

	XMoveResizeWindow(dpy, constraint_win, bound.x, bound.y, bound.width, bound.height);

	if (!(XGrabPointer(dpy, root, False, MouseMask, GrabModeAsync, GrabModeAsync, constraint_win, resizeend_curs, CurrentTime) == GrabSuccess))
	{
		return;
	}

	// create and map resize window
	resize_pattr.override_redirect = True;
	resize_pattr.background_pixel = inactive_col.pixel;
	resize_pattr.border_pixel = border_col.pixel;
	resize_pattr.event_mask = ChildMask|ButtonPressMask|ExposureMask|EnterWindowMask;
	resize_win = XCreateWindow(dpy, root,
		newdims.x, newdims.y - BARHEIGHT(), newdims.width + 1, newdims.height + BARHEIGHT() + 1, DEF_BORDERWIDTH,
		DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask, &resize_pattr);
	XMapWindow(dpy, resize_win);

	resizebar_pattr.override_redirect = True;
	resizebar_pattr.background_pixel = active_col.pixel;
	resizebar_pattr.border_pixel = border_col.pixel;
	resizebar_pattr.event_mask = ChildMask|ButtonPressMask|ExposureMask|EnterWindowMask;
	resizebar_win = XCreateWindow(dpy, resize_win,
		-DEF_BORDERWIDTH, -DEF_BORDERWIDTH, newdims.width + 1, BARHEIGHT() - DEF_BORDERWIDTH, DEF_BORDERWIDTH,
		DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask, &resizebar_pattr);
	XMapWindow(dpy, resizebar_win);

	write_titletext(c, resizebar_win);

	do
	{
		XMaskEvent(dpy, ExposureMask|MouseMask, &ev);
		switch (ev.type)
		{
			case Expose:
				exposed_c = find_client(ev.xexpose.window, FRAME);
				if (exposed_c)
				{
					redraw(exposed_c);
				}
				break;
			case MotionNotify:
				recalc_sweep(c, ev.xmotion.x, ev.xmotion.y, &newdims);
				XResizeWindow(dpy, resize_win, newdims.width + 1, newdims.height + BARHEIGHT() + 1);
				XResizeWindow(dpy, resizebar_win, newdims.width + 1, BARHEIGHT() - DEF_BORDERWIDTH);
				write_titletext(c, resizebar_win);
				break;
		}
	}
	while (ev.type != ButtonRelease);

	XUngrabServer(dpy);
	ungrab();
	last_focused_client = copy_focused;
	c->x = newdims.x;
	c->y = newdims.y;
	c->width = newdims.width + 1;
	c->height = newdims.height + 1;
	XMoveResizeWindow(dpy, c->frame, c->x, c->y - BARHEIGHT(), c->width, c->height + BARHEIGHT());
	XMoveResizeWindow(dpy, c->window, 0, BARHEIGHT(), c->width, c->height);
	send_config(c);
	XDestroyWindow(dpy, constraint_win);
	XDestroyWindow(dpy, resizebar_win);
	XDestroyWindow(dpy, resize_win);
}

static void recalc_sweep(Client *c, int x2, int y2, Rect *newdims)
{
	int dw, dh;
	dw = DisplayWidth(dpy, screen);
	dh = DisplayHeight(dpy, screen);
	newdims->width = (x2 - newdims->x) - BORDERWIDTH(c);
	newdims->height = (y2 - newdims->y) - BORDERWIDTH(c);
	get_incsize(c, &newdims->width, &newdims->height, newdims, PIXELS);

	if (c->size->flags & PMinSize)
	{
		if (newdims->width < c->size->min_width)
		{
			newdims->width = c->size->min_width;
		}
		if (newdims->height < c->size->min_height)
		{
			newdims->height = c->size->min_height;
		}
	}

	if (c->size->flags & PMaxSize)
	{
		if (newdims->width > c->size->max_width)
		{
			newdims->width = c->size->max_width;
		}
		if (newdims->height > c->size->max_height)
		{
			newdims->height = c->size->max_height;
		}
	}

	if (newdims->width < MINWINWIDTH)
	{
		newdims->width = MINWINWIDTH;
	}
	if (newdims->height < MINWINHEIGHT)
	{
		newdims->height = MINWINHEIGHT;
	}

	if (newdims->width > (dw - 1))
	{
		newdims->width = dw - 1;
	}
	if (newdims->height > dh - (BARHEIGHT() * 2) - 1)
	{
		newdims->height = dh - (BARHEIGHT() * 2) - 1;
	}
}

/* If the window in question has a ResizeInc int, then it wants to be
 * resized in multiples of some (x,y). Here we set x_ret and y_ret to
 * the number of multiples (if mode == INCREMENTS) or the correct size
 * in pixels for said multiples (if mode == PIXELS). */

static int get_incsize(Client *c, int *x_ret, int *y_ret, Rect *newdims, int mode)
{
	int basex, basey;
	if (c->size->flags & PResizeInc)
	{
		basex = (c->size->flags & PBaseSize) ? c->size->base_width :
			(c->size->flags & PMinSize) ? c->size->min_width : 0;
		basey = (c->size->flags & PBaseSize) ? c->size->base_height :
			(c->size->flags & PMinSize) ? c->size->min_height : 0;
		// work around broken apps that set their resize increments to 0
		if (mode == PIXELS)
		{
			if (c->size->width_inc != 0)
				*x_ret = newdims->width - ((newdims->width - basex) % c->size->width_inc);
			if (c->size->height_inc != 0)
				*y_ret = newdims->height - ((newdims->height - basey) % c->size->height_inc);
		}
		else // INCREMENTS
		{
			if (c->size->width_inc != 0)
				*x_ret = (newdims->width - basex) / c->size->width_inc;
			if (c->size->height_inc != 0)
				*y_ret = (newdims->height - basey) / c->size->height_inc;
		}
		return 1;
	}
	return 0;
}

void write_titletext(Client *c, Window bar_win)
{
#ifdef MWM_HINTS
	if (!c->has_title)
	{
		return;
	}
#endif
	if (!c->trans && c->name)
	{
#ifdef XFT
		XftDrawString8(c->xftdraw, &xft_detail,
			xftfont, SPACE, SPACE + xftfont->ascent,
			c->name, strlen(c->name));
#else
		XDrawString(dpy, bar_win, text_gc,
			SPACE, SPACE + font->ascent,
			c->name, strlen(c->name));
#endif
	}
}

