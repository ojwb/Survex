/*	xcaverot.c

  X-Windows cave viewing program

  This program is intended to be used in conjunction with the SURVEX program
  for cave survey data processing. It has been developed in conjunction with
  SURVEX version 0.20 (Beta).

  Now modified to work with SURVEX version 0.51 - should be backwards
  compatible.

  use:

        xcaverot [image file] [X-options]

  Copyright 1993 Bill Purvis

   Bill Purvis DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
   ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
   Bill Purvis BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
   ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
   WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
   ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
   SOFTWARE.

  History:

  28.06.93 wrote basic X code
  30.06.93 use of mouse to select centre-of-focus
  01.07.93 labels and crosses ?

1994.03.04 hacked around to use img.c to read .3d files [Olly]
1994.05.17 added changes from Andy Holtsbery
1994.06.01 should now be able to load image file given on command line [Olly]
1994.08.13 removed erroneous '*' & check for defined MININT and MAXINT [Olly]
1995.03.16 labels were being trimmed wrongly so prefix was displayed [Olly]
1997.01.09 checks argument 1 given before using it [Olly]

  17.04.97 adjusted way labels are drawn [JPNP]
  18.04.97 added elevation controls [JPNP]
  20.04.97 compass and elevation indicators, scale bar [JPNP}
  21.04.97 various little bits [JPNP]

1997.05.28 rearranged a fair bit, so we can merge with caverot soon [Olly]
1997.06.02 mostly converted to use cvrotimg [Olly]
1998.03.04 merged two deviant versions:
>1997.06.03 tweaked to compile [Olly]
>1997.06.10 fixed gcc warning [Olly]
1998.03.21 fixed up to compile cleanly on Linux
  */

/* Width of each button along the top of the window */
#define BUTWIDTH 60
#define BUTHEIGHT 25
#define FONTSPACE 20

/* Width and height of compass and elevation indicator windows */
#define INDWIDTH 100
#define INDDEPTH (INDWIDTH + FONTSPACE)

#define C_IND_ANG 25
#define E_IND_ANG 11
#define E_IND_LEN 0.8
#define E_IND_LINE 0.6
#define E_IND_PTR 0.38

/* radius of compass and clino indicators */
#define RADIUS ((float)INDWIDTH*.4)

/* font to use */
#define FONTNAME "8x13"

#define XOFF 500
#define YOFF 350
#define PIBY180 0.017453293

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "useful.h"
#include "img.h"
#include "error.h"
#include "caverot.h"
#include "cvrotimg.h"

#ifndef MAXINT
#define MAXINT 0x7fffffff
#endif

#ifndef MININT
#define MININT 0x80000000
#endif

#if 0
#define MOVE 1
#define DRAW 2
#define STOP 4
#define CROSS 8
#define LABEL 16
#endif

#define PLAN 1               /* values of plan_elev */
#define ELEVATION 2

/*static point *pdata = NULL;*/
/*static int npoints;*/

/* scale all data by this to fit in coord data type */
/* Note: data in file in metres. 100.0 below stores to nearest cm */

static float factor = (float)100.0;

static float scale = 0.1;
static float zoomfactor = 1.2;
static float sbar;
static float scale_orig;
static int changedscale = 1;
static int scale_y;


static float angle = 0.0;
static float elev_angle = 0.0;
static float rot_step = +3.0;

static int plan_elev;
static int rot = 0;
static int crossing = 0;
static int labelling = 0;

static int xoff, yoff;  /* offsets at which survey is plotted in window */
static coord x_min=0;
static coord x_max=0;
static coord y_min=0;
static coord y_max=0;
static coord z_min=0;
static coord z_max=0;
static coord x_mid;
static coord y_mid;
static coord z_mid;
static Window mywindow;
static Window butzoom, butmooz, butload, butrot, butstep, butquit;
static Window butplan, butlabel, butcross, butselect;
static Display *mydisplay;
static GC mygc, scale_gc, slab_gc;
static Window ind_com, ind_elev, scalebar;
static Region label_reg;

static XFontStruct *fontinfo;
static char hello[] = "X-Caverot";
static Font font;

static unsigned long black,white;

static int lab_col_ind = 19; /* JPNP */
static int fontheight, slashheight;       /* JPNP */

static char *ncolors[] = { "black",
  "BlanchedAlmond", "BlueViolet",
  "CadetBlue1", "CornflowerBlue", "DarkGoldenrod", "DarkGreen",
  "DarkKhaki", "DarkOliveGreen1", "DarkOrange", "DarkOrchid",
  "DarkSalmon", "DarkSeaGreen", "DarkSlateBlue", "DarkSlateGray",
  "DarkViolet", "DeepPink", "DeepSkyBlue", "DodgerBlue",
  "ForestGreen", "GreenYellow",
  "HotPink", "IndianRed", "LawnGreen",
  "LemonChiffon", "LightBlue", "LightCoral",
  "LightGoldenrod", "LightGoldenrodYellow", "LightGray", "LightPink",
  "LightSalmon", "LightSeaGreen", "LightSkyBlue", "LightSlateBlue",
  "LightSlateGray", "LightSteelBlue", "LimeGreen",
  "MediumAquamarine", "MediumOrchid", "MediumPurple", "MediumSeaGreen",
  "MediumSlateBlue", "MediumSpringGreen", "MediumTurquoise", "MediumVioletRed",
  "MidnightBlue", "MistyRose", "NavajoWhite",
  "NavyBlue", "OldLace", "OliveDrab", "OrangeRed1",
  "PaleGoldenrod", "PaleGreen", "PaleTurquoise", "PaleVioletRed",
  "PapayaWhip", "PeachPuff", "PowderBlue", "RosyBrown",
  "RoyalBlue", "SandyBrown", "SeaGreen1", "SkyBlue",
  "SlateBlue", "SlateGray", "SpringGreen", "SteelBlue",
  "VioletRed", "WhiteSmoke", "aquamarine", "azure",
  "beige", "bisque", "blue",  "brown",
  "burlywood", "chartreuse1", "chocolate",
  "coral", "cornsilk", "cyan", "firebrick",
  "gainsboro", "gold1", "goldenrod", "green1",
  "honeydew", "ivory", "khaki", "lavender",
  "linen", "magenta", "maroon", "moccasin",
  "orange", "orchid", "pink", "plum",
  "purple", "red1", "salmon", "seashell",
  "sienna", "snow", "tan", "thistle",
  "tomato1", "turquoise", "violet", "wheat",
  "gray12", "gray24", "gray36",
  "gray48", "gray60", "gray73", "gray82", "gray97", NULL };

static Colormap color_map;
static XColor colors[128];
static XColor exact_colors[128];
static char *surveys[128];
static char surveymask[128];
static GC gcs[128];
static int numsurvey = 0;

/* Create a set of colors from named colors */

static void color_set_up( Display *display, Window window ) {
   int i;
   XGCValues vals;

   color_map = DefaultColormap(display,0);

   /* get the colors from the color map for the colors named */
   for ( i = 0; ncolors[i]; i++ ) {
      XAllocNamedColor( display, color_map, ncolors[i],
		        &exact_colors[i], &colors[i] );
      vals.foreground = colors[i].pixel;
      gcs[i] = XCreateGC( display, window, GCForeground, &vals );
   }
   numsurvey = 0;
}

static void flip_button( Display *display, Window mainwin, Window button,
		  GC normalgc, GC inversegc, char *string ) {
   XClearWindow(display, button);
   XFillRectangle(display, button, normalgc, 0, 0, BUTWIDTH, 25);
   XDrawImageString(display, button, inversegc, BUTWIDTH/4, 20, string, strlen(string));
   XFlush(display);
}

int findsurvey( char *name ) {
   int i;

   for ( i = 0; i < numsurvey; i++ )
      if (strcmp(surveys[i], name) == 0)
         return i;
   i = numsurvey;
   if (ncolors[i+1])
       numsurvey++;
   surveys[i] = (char *)osmalloc(strlen(name)+1);
   strcpy(surveys[i], name);
   surveymask[i] = 1;
   return i;
}

void update_limits( point *p ) {
  if (p->X < x_min)
    x_min = p->X;
  if (p->X > x_max)
    x_max = p->X;
  if (p->Y < y_min)
    y_min = p->Y;
  if (p->Y > y_max)
    y_max = p->Y;
  if (p->Z < z_min)
    z_min = p->Z;
  if (p->Z > z_max)
    z_max = p->Z;
}

static lid Huge **ppLegs=NULL;
static lid Huge **ppStns=NULL;

int load_file( const char *name ) {
   ppLegs = osmalloc((1 + 1) * sizeof(lid Huge *));
   ppStns = osmalloc((1 + 1) * sizeof(lid Huge *));
   /* load data into memory */
   if (!load_data( name, ppLegs, ppStns) )  return 0;
   ppLegs[1] = NULL;
   ppStns[1] = NULL;
   return 1;
#if 0
 x_min = MAXINT;
 x_max = MININT;
 y_min = MAXINT;
 y_max = MININT;
 z_min = MAXINT;
 z_max = MININT;

 srvy = 0;	/* default survey number */
 surveymask[0] = 1;	/* enable default survey display */

      /* srvy = findsurvey(sz); */ /* findsurvey(p); */

    update_limits(pData+c);

 x_mid = (x_min + x_max) / 2;
 y_mid = (y_min + y_max) / 2;
 z_mid = (z_min + z_max) / 2;
#endif
}

void process_load(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  Window enter_window;
  XEvent enter_event;
/*  Font enter_font; */
  GC enter_gc;
  KeySym key;
  char string[128];
  char text[10];
  int count;

  flip_button(display, mainwin, button, mygc, egc, "Load");
  numsurvey = 0;

  /* set up input window */
  enter_window = XCreateSimpleWindow(display, mainwin,
				     340, 200,
				     300, 100,
				     3,black,white);
  enter_gc = mygc;
  XMapRaised(display,enter_window);
  XClearWindow(display,enter_window);
  XSelectInput(display,enter_window,ButtonPressMask|KeyPressMask);

  for (;;)
    {
      /* reset everything */
      XDrawString(display,enter_window,mygc,
		  10,50,"Enter file name",15);
      XFlush(display);
      string[0] = '\0';
      key = 0;

      /* accept input from the user */
      do
	{
	  XNextEvent(display,&enter_event);
	  count = XLookupString((XKeyEvent *)&enter_event,text,10,&key,0);
	  text[count] = '\0';
	  if (key != XK_Return)
	    strcat(string,text);
	  XClearWindow(display, enter_window);
	  XDrawString(display,enter_window,mygc,
		      10,50,"Enter file name",15);
	  XFlush(display);
	  XDrawString(display,enter_window,enter_gc,
		      100,75,string,strlen(string));
	}
      while (key != XK_Return &&
	     enter_event.type == KeyPress );

      /* Clear everything out of the buffers and reset */
      XFlush(display);
      if (!load_file( string )) {
         strcpy( string, "File not found or not a Survex image file" );
    	 break;
      }

      XClearWindow(display, enter_window);
      XDrawString(display, enter_window, enter_gc,
		  10, 25, string, strlen(string));
    }  /* End of for (;;) */

  XDestroyWindow(display, enter_window);

  flip_button(display, mainwin, button, egc, mygc, "Load");
}

void process_plan(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, plan_elev==ELEVATION ? "Elev" : "Plan");
  if (plan_elev == PLAN) { plan_elev = ELEVATION; elev_angle = 0.0; }
       else { plan_elev = PLAN; elev_angle = 90.0; }
  flip_button(display, mainwin, button, egc, mygc, plan_elev==ELEVATION ? "Elev" : "Plan");
}

void process_label(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, labelling ? "No Label" : "Label");
  labelling = (labelling == 0);
  flip_button(display, mainwin, button, egc, mygc, labelling ? "No Label" : "Label");
}

void process_cross(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, crossing ? "No Cross" : "Cross");
  crossing = (crossing == 0);
  flip_button(display, mainwin, button, egc, mygc, crossing ? "No Cross" : "Cross");
}

void process_rot(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, rot ? "Stop" : "Rotate");
  rot = (rot == 0);
  flip_button(display, mainwin, button, egc, mygc, rot ? "Stop" : "Rotate");
}

void process_step(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, "Step");
  angle += 3.0;
  if (angle >= 360.0)
    angle = 0.0;
  flip_button(display, mainwin, button, egc, mygc, "Step");
}

void process_zoom(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, "Zoom in");
  scale *= zoomfactor;
  changedscale = 1;
  flip_button(display, mainwin, button, egc, mygc, "Zoom in");
}

void process_mooz(Display *display, Window mainwin, Window button,
		  GC mygc, GC egc)
{
  flip_button(display, mainwin, button, mygc, egc, "Zoom out");
  scale /= zoomfactor;
  changedscale = 1;
  flip_button(display, mainwin, button, egc, mygc, "Zoom out");
}

void process_select(Display *display, Window window, Window button, GC mygc, GC egc)
{
  Window select;
  XEvent event;
  int n, wid, ht, x, y, i;

  flip_button(display, window, button, mygc, egc, "select");
  for (n=1; n*n*5 < numsurvey; n++)
    ;
  ht = n * 5 * 20;
  wid = n * 130;
  select = XCreateSimpleWindow(display, window,
			       0, 30, wid, ht, 3, black, white);
  XMapRaised(display, select);

  XSelectInput(display, select, ButtonPressMask);
  for (;;)
    {
      XClearWindow(display, select);
      x = 0;
      y = 0;
      for (i=0; i<numsurvey; i++)
	{
	  XFillRectangle(display, select, gcs[i], x, y, 10, 10);
	  XDrawRectangle(display, select, mygc, x+12, y, 10, 10);
	  if (surveymask[i])
	    {
	      XDrawLine(display, select, mygc, x+12, y, x+21, y+9);
	      XDrawLine(display, select, mygc, x+12, y+9, x+21, y);
	    }
	  XDrawString(display, select, mygc, x+25, y+10,
		      surveys[i], strlen(surveys[i]));
	  /* XDrawString(display, select, mygc, x+100, y+10,
	     ncolors[i], strlen(ncolors[i])); */
	  y += 20;
	  if (y >= ht)
	    {
	      y = 0;
	      x += 130;
	    }
	}
      XNextEvent(display, &event);
      if (event.type == ButtonPress)
	{
	  XButtonEvent *e = (XButtonEvent *)&event;

	  x = e->x;
	  y = e->y;
	  printf("select: x=%d, y=%d\n", x, y);
	  i = 0;
	  while (x > 130)
	    {
	      x -= 130;
	      i += n*5;
	      printf("adjust: x=%d, i=%d\n", x, i);
	    }
	  i += (y / 20);
	  printf("final i=%d\n", i);
	  if (i >= numsurvey)
	    break;
	  if (x >= 12 && x < 22)
	    surveymask[i] = 1 - surveymask[i];
	}
    }
  XDestroyWindow(display, select);
  flip_button(display, window, button, egc, mygc, "select");
}

void draw_ind_elev( Display *display, GC gc, float angle ) {
  char temp[32];
  int xm,ym;
  float sa, ca;
  float r = (float)INDWIDTH*E_IND_LINE/2;
  float q = (float)INDWIDTH*E_IND_PTR/2;

  xm = ym = INDWIDTH/2 ;

  sa = sin(angle * PIBY180);
  ca = cos(angle * PIBY180);

/*printf("%f\n",ca);*/

  XClearWindow(mydisplay, ind_elev);

  XDrawLine(display, ind_elev, gc, xm-r, ym, xm+r, ym);

  XDrawLine(display, ind_elev, gc, xm-q, ym, xm-q + 2*q*(ca+0.001), ym - 2*q*sa);
  XDrawLine(display, ind_elev, gc, xm-q, ym, xm-q + E_IND_LEN*q*cos((angle+E_IND_ANG)*PIBY180),
	    ym - E_IND_LEN*q*sin((angle+E_IND_ANG)*PIBY180));
  XDrawLine(display, ind_elev, gc, xm-q, ym, xm-q + E_IND_LEN*q*cos((angle-E_IND_ANG)*PIBY180),
	    ym - E_IND_LEN*q*sin((angle-E_IND_ANG)*PIBY180));


  sprintf(temp, "%d", (int)angle);
  XDrawString(display, ind_elev, gc, INDWIDTH/2-20, INDDEPTH-10, temp, strlen(temp));

}

void draw_ind_com( Display *display, GC gc, float angle ) {
  char temp[32];
  int xm,ym;
  float sa, ca;
  float rs, rc, r = RADIUS;

  xm = ym = INDWIDTH/2 ;

  rs = r * sin(C_IND_ANG*PIBY180);
  rc = r * cos(C_IND_ANG*PIBY180);
  sa = sin((angle+180) * PIBY180);
  ca = cos((angle+180) * PIBY180);

  XClearWindow(mydisplay, ind_com);

  XDrawArc(display, ind_com, gc, xm-(int)r, ym-(int)r, 2*(int)r, 2*(int)r, 0, 360*64);

  XDrawLine(display, ind_com, gc, xm +r*sa, ym -r*ca, xm -rs*ca - rc*sa, ym -rs*sa + rc*ca);
  XDrawLine(display, ind_com, gc, xm +r*sa, ym -r*ca, xm +rs*ca - rc*sa, ym +rs*sa + rc*ca);
  XDrawLine(display, ind_com, gc, xm -rs*ca - rc*sa, ym -rs*sa + rc*ca, xm -r*sa/2, ym +r*ca/2);
  XDrawLine(display, ind_com, gc, xm +rs*ca - rc*sa, ym +rs*sa + rc*ca, xm -r*sa/2, ym +r*ca/2);
  XDrawLine(display, ind_com, gc, xm, ym-r/2, xm, ym -r);

  sprintf(temp, "%03d", 180-(int)angle);
  XDrawString(display, ind_com, gc, INDWIDTH/2-20, INDDEPTH-10, temp, strlen(temp));
}

void draw_scalebar( void ) {
  char temp[20];
  float l,m,n,o;

  if (changedscale) {

    XWindowAttributes a;

    XGetWindowAttributes(mydisplay, scalebar, &a);

    l = (float)(a.height-4)/(factor*scale);

    m = log10(l) ;

    n = pow(10,floor(m));                               /* Ouch did I really do all this ;-) JPNP */

    o = (m-floor(m) < log10(5) ? n : 5*n );

    sbar = (int)o;

    changedscale =0;
  }
 /* printf("%f\t%f\t%f\t%f\n", l,m,n,o); */

  XClearWindow(mydisplay, scalebar);

  XDrawLine(mydisplay, scalebar, scale_gc, 13, 2, 13, 2 + scale*factor*sbar);

  if (sbar<1000)
    sprintf(temp, "%d m", (int)sbar);
  else
    sprintf(temp, "%d km", (int)sbar/1000);
  XDrawString(mydisplay, mywindow, slab_gc, 8, BUTHEIGHT + FONTSPACE, temp, strlen(temp));
}

void draw_label( Display *display, Window window, GC gc, int x, int y, const char *string, int length ) {
  XRectangle r;
  int strwidth;
  int width=1000, height=700;
  strwidth = XTextWidth( fontinfo, string, length );

  if (x<width && y<height && x+strwidth>0 && y+fontheight>0){

    if (XRectInRegion(label_reg, x,y,strwidth,fontheight) == RectangleOut) {
      r.x=x;
      r.y=y;
      r.width=strwidth;
      r.height=fontheight;
      XUnionRectWithRegion(&r,label_reg,label_reg);
      XDrawString(display,window,gc,x,y, string, length);
    }
  }
}

static float sx,cx,fx,fy,fz;
void setview( float angle ) {
   sx = sin((angle)*PIBY180);
   cx = cos((angle)*PIBY180);
   fz = cos(elev_angle*PIBY180);
   fx = sin(elev_angle*PIBY180) * sx;
   fy = sin(elev_angle*PIBY180) * -cx;
}

int toscreen_x( point *p ) {
   return (((p->X - x_mid) * -cx + (p->Y - y_mid) * -sx) * scale) + xoff;
}

int toscreen_y( point *p ) {
   int y;
   switch (plan_elev) {
    case PLAN:
      y = ((p->X - x_mid) * sx - (p->Y - y_mid) * cx) * scale;
    case ELEVATION:
      y = (p->Z - z_mid) * scale;
    default:
      y = ((p->X - x_mid) * fx + (p->Y - y_mid) * fy
         + (p->Z - z_mid) * fz) * scale;
   }
   return yoff - y;
}

point *find_station( int x, int y, int mask ) {
   point *p, *q = NULL;
   /* d_min is some measure of how close we are (e.g. distance squared,
    * min( dx, dy ), etc) */
   int d_min = MAXINT;

   if (ppStns == NULL) return NULL;

   setview( angle );

   for ( p = ppStns[0]->pData; p->_.str != NULL; p++ ) {
      int d;
      int x1 = toscreen_x( p );
      int y1 = toscreen_y( p );
#if 1
      d = (x1 - x) * (x1 - x) + (y1 - y) * (y1 - y);
#else
      d = min( abs(x1 - x), abs(y1 - y) );
#endif
      if (d < d_min) {
         d_min = d;
         q = p;
      }
   }

   printf( "near %d, %d, station %s was found\nat %d, %d\t\t%d, %d, %d\n",
           x, y, q->_.str, toscreen_x(q), toscreen_y(q), q->X, q->Y, q->Z );
   return q;
}

void redraw_image( Display *display, Window window, GC gc ) {
  point *p;
  coord x1, y1, x2, y2;
  XWindowAttributes a;
  int width, height;
  int srvy=1;

  x1 = y1 = 0; /* avoid compiler warning */

  if (ppStns == NULL && ppLegs == NULL) return;

  XClearWindow(display, window);
  XGetWindowAttributes(display, window, &a);
  height = a.height;
  width = a.width;
  xoff = width / 2;
  yoff = height / 2;

  label_reg = XCreateRegion();

  if (rot) angle += rot_step;

  while (angle > 180.0)
   angle -= 360.0;
  while (angle <= -180.0)
   angle += 360.0;

  if (elev_angle == 0.0) plan_elev=ELEVATION;
  else if (elev_angle == 90.0) plan_elev=PLAN;
  else plan_elev=0;

  setview( angle );

  /* printf("height=%d, width=%d, xoff=%d, yoff=%d\n", height, width, xoff, yoff); */

  for ( p = ppLegs[0]->pData; p->_.action != STOP; p++ ) {
      switch (p->_.action) {
       case MOVE:
//         srvy = p->survey;
	 x1 = toscreen_x( p );
	 y1 = toscreen_y( p );
	 break;
       case DRAW:
	 x2 = toscreen_x( p );
	 y2 = toscreen_y( p );
//	 if (surveymask[srvy])
	    XDrawLine( display, window, gcs[srvy], x1, y1, x2, y2 );
 	    /*printf( "draw line from %d,%D to %d,%d\n", x1, y1, x2, y2 );*/
 	 x1 = x2;
	 y1 = y2;	/* for continuous drawing */
	 break;
      }
   }
	
   if ((crossing || labelling) /*&& surveymask[p->survey]*/) {
      for ( p = ppStns[0]->pData; p->_.str != NULL; p++ ) {
         x2 = toscreen_x( p );
         y2 = toscreen_y( p );
         if (crossing) {
            XDrawLine( display, window, gcs[srvy],
	          		  x2-10, y2, x2+10, y2 );
            XDrawLine( display, window, gcs[srvy],
                      x2, y2-10, x2, y2+10 );
         }
	     if (labelling) {
            char *q;
            q = p->_.str;
            draw_label( display, window, gcs[lab_col_ind],
		        x2, y2 + slashheight, q, strlen(q) );
            /* XDrawString(display,window,gcs[lab_col_ind],x2,y2+slashheight, q, strlen(q)); */
            /* XDrawString(display,window,gcs[p->survey],x2+10,y2, q, strlen(q)); */
         }
      }
   }
   
/*   XDestroyRegion( label_reg ); */
   draw_ind_com( display, gc, angle );
   draw_ind_elev( display, gc, elev_angle );
   draw_scalebar();
   XFlush( display );
}

void process_focus( Display *display, Window window, int ix, int iy ) {
  int height, width;
  float x, y, sx, cx;
  XWindowAttributes a;
  point *q;

  XGetWindowAttributes(display, window, &a);
  height = a.height / 2;
  width = a.width / 2;

  x = (int)((float)(-ix + width) / scale);
  y = (int)((float)(height - iy) / scale);
  sx = sin(angle*PIBY180);
  cx = cos(angle*PIBY180);
  /* printf("process focus: ix=%d, iy=%d, x=%lf, y=%lf\n", ix, iy, (double)x, (double)y);*/
  if (plan_elev == PLAN) {
      x_mid += x * cx + y * sx;
      y_mid += x * sx - y * cx;
  } else if (plan_elev == ELEVATION) {
      z_mid += y;
      x_mid += x * cx;
      y_mid += x * sx;
  } else {
      if ( (q=find_station(ix,iy,MOVE | DRAW /*| LABEL*/)) != NULL) {
        float n1, n2, n3, j1, j2, j3, i1, i2, i3;
        /*
	x_mid = q->X;
	y_mid = q->Y;
	z_mid = q->Z;
	*/

	n3 = cos(elev_angle*PIBY180);
        n1 = sx*n3;
        n2 = cx*n3;
        n3 = sin(elev_angle*PIBY180);

	x = (ix - toscreen_x(q)) / scale;
	y = (iy - toscreen_y(q)) / scale;

        i1 = cx;
        i2 = sx;
        i3 = 0;

        j1 = n2*i3 - n3*i2;
        j2 = n3*i1 - n1*i3;
        j3 = n1*i2 - n2*i1;

	x_mid = q->X + x*i1 + y*j1;
        y_mid = q->Y + x*i2 + y*j2;
        z_mid = q->Z + x*i3 + y*j3;
	printf("vector (%f,%f,%f)\n", x*i1 + y*j1, x*i2 + y*j2, x*i3 + y*j3);
	printf("x_mid, y_mid moved to %d,%d,%d\n", x_mid, y_mid, z_mid);
       }
    }

  /*printf("x_mid, y_mid moved to %lf,%lf\n", (double)x_mid, (double)y_mid);*/
}

void draw_string( Window win, int x, int y, char *str ) {
   XDrawImageString( mydisplay, win, mygc, x, y, str, strlen(str) );
}

void draw_buttons( void ) {
   draw_string(butload, BUTWIDTH/4, 20, "Load");
   draw_string(butrot , BUTWIDTH/4, 20, rot ? "Stop" : "Rotate");
   draw_string(butstep, BUTWIDTH/4, 20, "Step");
   draw_string(butzoom, BUTWIDTH/4, 20, "Zoom in");
   draw_string(butmooz, BUTWIDTH/4, 20, "Zoom out");
   draw_string(butplan, BUTWIDTH/4, 20, plan_elev==ELEVATION ? "Elev" :(plan_elev==PLAN ? "Plan": "Tilt"));
   draw_string(butlabel,BUTWIDTH/4, 20, labelling ? "No Label" : "Label");
   draw_string(butcross,BUTWIDTH/4, 20, crossing ? "No Cross" : "Cross");
   draw_string(butselect,BUTWIDTH/4,20, "Select");
   draw_string(butquit, BUTWIDTH/4, 20, "Quit");
}

void drag_compass( int x, int y ) {
   /* printf("x %d, y %d, ", x,y);*/
   x -= INDWIDTH/2;
   y -= INDWIDTH/2;
   /* printf("xm %d, y %d, ", x,y);*/
/*   angle = atan((-x)/(y?y:0.01))/PIBY180 + (y<0 ? 180.0 : 0.0); */
   angle = atan2( -x, y ) / PIBY180;
   /* snap angle to nearest 45 degrees if outside circle */
   if (x*x + y*y > RADIUS * RADIUS) angle = ((int)((angle + 360 + 22) / 45 ))*45 - 360;
   /* printf("a %f\n", angle); */
}

void drag_elevation( int x, int y ) {
   x -= INDWIDTH*(1-E_IND_PTR)/2;
   y -= INDWIDTH/2;

   if (x >= 0) {
      elev_angle = atan2( -y, x )/PIBY180;
   } else {
      /* if the mouse is to the left of the elevation indicator,
       * snap to view from -90, 0 or 90 */
      if ( abs( y ) < INDWIDTH*E_IND_PTR/2 ) {
         elev_angle = 0.0f;
      } else if (y<0) {
         elev_angle = 90.0f;
      } else {
         elev_angle = -90.0f;
      }
   }
}



int main( int argc, char **argv ) {
/*  Window hslide, vslide;*/
  XWindowAttributes attr;
/*  XWindowChanges ch; */
  XGCValues gcval;

  int oldwidth,oldheight;
  GC enter_gc;
  XEvent myevent;
  XSizeHints myhint;
  XGCValues gvalues;
/*  XComposeStatus cs; */
  int myscreen;
  unsigned long myforeground, mybackground, ind_fg, ind_bg;
  int done;

  angle = 180;
  elev_angle = 90.0;

  /* set up display and foreground/background */
  mydisplay = XOpenDisplay("");
  myscreen = DefaultScreen (mydisplay);
  white = mybackground = ind_bg = WhitePixel (mydisplay, myscreen);
  black = myforeground = ind_fg = BlackPixel (mydisplay, myscreen);

  /* create a window with a size of 1000 x 700 */
  myhint.x = 0;
  myhint.y = 0;
  myhint.width = 1000;
  myhint.height = 700;
  myhint.flags = /* PPosition |*/
                 PSize;
  mywindow = XCreateSimpleWindow (mydisplay,
				  DefaultRootWindow (mydisplay),
				  myhint.x, myhint.y, myhint.width, myhint.height,
				  5, myforeground, mybackground);

  XGetWindowAttributes(mydisplay, mywindow, &attr);

  /* create children windows that will act as menu buttons */
  butload = XCreateSimpleWindow (mydisplay,mywindow,
		0,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butrot  = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butstep = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*2,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butzoom = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*3,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butmooz = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*4,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butplan = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*5,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butlabel = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*6,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butcross = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*7,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butselect = XCreateSimpleWindow (mydisplay,mywindow,
		BUTWIDTH*8,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);
  butquit = XCreateSimpleWindow (mydisplay,mywindow,
  		BUTWIDTH*9,0,BUTWIDTH,BUTHEIGHT,2,myforeground,mybackground);

  /* children windows to act as indicators */
  ind_com = XCreateSimpleWindow(mydisplay, mywindow, attr.width - INDWIDTH-1,0,
  				INDWIDTH, INDDEPTH, 1, ind_fg, ind_bg);
  ind_elev = XCreateSimpleWindow(mydisplay, mywindow, attr.width - INDWIDTH*2 -1 , 0,
  				 INDWIDTH, INDDEPTH, 1, ind_fg, ind_bg);
  scalebar = XCreateSimpleWindow(mydisplay, mywindow, 0, BUTHEIGHT+5+FONTSPACE, 23, attr.height - (BUTHEIGHT
				 +FONTSPACE+5), 0, ind_fg, ind_bg);

  /* so we can auto adjust scale on window resize */
  oldwidth = attr.width;
  oldheight = attr.height;

  /* try to open file if we've been given one */
  if (argv[1] && argv[1][0]!='\0' && argv[1][0]!='-') {
     /* if it loaded okay then discard the command line argument */
     if (load_file( argv[1] )) {
        argc--;
        argv++;
     }
  }

  /* print program name at the top */
  XSetStandardProperties (mydisplay, mywindow, hello, hello,
			  None, argv, argc, &myhint);

  /* set up foreground/backgrounds and set up colors */
  mygc = XCreateGC (mydisplay, mywindow, 0, 0);
  gvalues.foreground = myforeground;
  gvalues.background = mybackground;
  enter_gc = XCreateGC(mydisplay,butzoom, 0, 0);
  scale_gc = XCreateGC(mydisplay,mywindow, 0, 0);
  slab_gc = XCreateGC(mydisplay,mywindow, 0, 0);

  gcval.line_width =2;

  XChangeGC(mydisplay, scale_gc, GCLineWidth ,&gcval);

  /* Load up a font called FONTNAME (was 8x16 originally) */
  font = XLoadFont(mydisplay,FONTNAME);
  XSetFont(mydisplay,mygc,font);
  XSetFont(mydisplay,enter_gc,font);
  XSetBackground (mydisplay, mygc, mybackground);
  XSetForeground (mydisplay, mygc, myforeground);
  XSetBackground (mydisplay,enter_gc,myforeground);
  XSetForeground (mydisplay,enter_gc,mybackground);
  color_set_up(mydisplay, mywindow);

  /* Get height value for font to use in label positioning JPNP 26/3/97 */
  fontinfo  = XQueryFont(mydisplay, XGContextFromGC(gcs[0]) );
  if (fontinfo) {
      fontheight = (fontinfo->max_bounds).ascent ;
      if (fontinfo->per_char != NULL)
         slashheight = fontinfo->per_char['\\'].ascent;
      else
         slashheight = fontheight;

      /* printf("fontheight: %d\n",fontheight); */
  }
  else
      fontheight = slashheight = 10;


  /* Ask for input from the mouse and keyboard */
  XSelectInput (mydisplay, mywindow,
		ButtonPressMask | KeyPressMask | ExposureMask
		| StructureNotifyMask
		);
  XSelectInput (mydisplay, ind_com, ButtonPressMask | ButtonMotionMask);
  XSelectInput (mydisplay, ind_elev, ButtonPressMask | ButtonMotionMask);
  XSelectInput (mydisplay, scalebar, ButtonPressMask | ButtonMotionMask);

  /* map window to the screen */
  XMapRaised (mydisplay, mywindow);

  /* Map children windows to the screen */

  XMapRaised (mydisplay, butload);
  XMapRaised (mydisplay, butrot);
  XMapRaised (mydisplay, butstep);
  XMapRaised (mydisplay, butzoom);
  XMapRaised (mydisplay, butmooz);
  XMapRaised (mydisplay, butplan);
  XMapRaised (mydisplay, butlabel);
  XMapRaised (mydisplay, butcross);
  XMapRaised (mydisplay, butselect);
  XMapRaised (mydisplay, butquit);
  XMapRaised (mydisplay, ind_com);
  XMapRaised (mydisplay, ind_elev);
  XMapRaised (mydisplay, scalebar);

  /* Display menu strings */

  XNextEvent(mydisplay,&myevent);
  draw_buttons();

  /* Loop through until a q is press when the cursor is in
     the window, which will cause the application to quit */

  done = 0;
  while (done == 0)
    {
      if (rot == 0 || XPending(mydisplay))
	{
	  XNextEvent (mydisplay, &myevent );
	  if (myevent.type == ButtonPress)
	    {
	      if (myevent.xbutton.subwindow == butzoom)
		process_zoom(mydisplay, mywindow, butzoom, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butmooz)
		process_mooz(mydisplay, mywindow, butmooz, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butload)
		process_load(mydisplay, mywindow, butload, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butrot)
		process_rot(mydisplay, mywindow, butrot, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butstep)
		process_step(mydisplay, mywindow, butstep, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butplan)
		process_plan(mydisplay, mywindow, butplan, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butlabel)
		process_label(mydisplay, mywindow, butlabel, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butcross)
		process_cross(mydisplay, mywindow, butcross, mygc, enter_gc);
	      else if (myevent.xbutton.subwindow == butselect)
		process_select(mydisplay, mywindow, butselect, mygc, enter_gc);

	      else if (myevent.xbutton.subwindow == butquit)
		{
		  done = 1;
		  break;
                }
	      else if (myevent.xbutton.window == ind_com)
                {
		  drag_compass(myevent.xbutton.x, myevent.xbutton.y);
		}
	      else if (myevent.xbutton.window == ind_elev)
		{
		  drag_elevation(myevent.xbutton.x, myevent.xbutton.y);
		}
	      else if (myevent.xbutton.window == scalebar)
		{
		  scale_y = myevent.xbutton.y;
		  scale_orig = scale;
		}
	      else if (myevent.xbutton.window == mywindow)
		process_focus(mydisplay, mywindow,
			      myevent.xbutton.x, myevent.xbutton.y);
	    }
	  if (myevent.type == MotionNotify)
	    {
	    if (myevent.xmotion.window == ind_com)
	        drag_compass(myevent.xmotion.x, myevent.xmotion.y);
	    if (myevent.xmotion.window == ind_elev)
	        drag_elevation(myevent.xmotion.x, myevent.xmotion.y);
	    if (myevent.xmotion.window == scalebar)
	      {
		if (myevent.xmotion.state & Button1Mask)
		    scale = exp(log(scale_orig)+(float)(myevent.xmotion.y - scale_y)/
		    	         (250) );
                else
	            scale = exp(log(scale_orig)*(1 - (float)(myevent.xmotion.y - scale_y)/100));

		changedscale=1;
	      }
	    }
	  if (myevent.xany.window == mywindow)
	    {
	      switch (myevent.type)
		{
	        case KeyPress:
		  {
		    char text[10];
		    KeySym mykey;
		    int i;

		    i = XLookupString ((XKeyEvent *)&myevent,
				       text, 10, &mykey, 0);
		    if (i == 1)
		        switch (text[0])
			  {
			  case 'q':
		            done = 1;
			    break;
			  case 'u':
			    elev_angle += 3.0;
			    if (elev_angle > 90.0 ) elev_angle = 90.0;
			    plan_elev = 0;
			    break;
			  case 'd':
			    elev_angle -= 3.0;
			    if (elev_angle < -90.0) elev_angle = -90.0;
			    plan_elev = 0;
			    break;
			  }
		    break;
		  } /* case  */
	          case ConfigureNotify:

		  changedscale  = 1;

		  scale *= min((float)myevent.xconfigure.width/(float)oldwidth, (float)myevent.xconfigure.height/(float)oldheight);

		  oldwidth = myevent.xconfigure.width;
		  oldheight = myevent.xconfigure.height;

		  XResizeWindow(mydisplay, scalebar, 23, myevent.xconfigure.height - BUTHEIGHT
				 -FONTSPACE-5);

		  XMoveWindow(mydisplay, ind_com, myevent.xconfigure.width-INDWIDTH-1, 0);
		  XMoveWindow(mydisplay, ind_elev, myevent.xconfigure.width-(2*INDWIDTH)-1, 0);

		  break;
		} /* switch */
	    } /* end if */
	}
      redraw_image(mydisplay, mywindow, mygc);
      draw_buttons();
    } /* while */

  /* Free up and clean up the windows created */
  XFreeGC (mydisplay, mygc);
  XDestroyWindow (mydisplay, mywindow);
  XCloseDisplay (mydisplay);
  exit(0);
}
