/* datain.c
 * Reads in survey files, dealing with special characters, keywords & data
 * Copyright (C) 1991-2003,2005,2009,2010,2011,2012,2013,2014,2015,2016,2017,2018 Olly Betts
 * Copyright (C) 2004 Simeon Warner
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <limits.h>
#include <stdarg.h>

#include "debug.h"
#include "cavern.h"
#include "date.h"
#include "filename.h"
#include "message.h"
#include "filelist.h"
#include "netbits.h"
#include "netskel.h"
#include "readval.h"
#include "datain.h"
#include "commands.h"
#include "out.h"
#include "str.h"
#include "thgeomag.h"

#define EPSILON (REAL_EPSILON * 1000)

#define var(I) (pcs->Var[(I)])

/* true if x is not-a-number value in Compass (999.0 or -999.0)    */
/* Compass uses 999.0 but understands Karst data which used -999.0 */
#define is_compass_NaN(x) ( fabs(fabs(x)-999.0) <  EPSILON )

int ch;

typedef enum {
    CTYPE_OMIT, CTYPE_READING, CTYPE_PLUMB, CTYPE_INFERPLUMB, CTYPE_HORIZ
} clino_type;

/* Don't explicitly initialise as we can't set the jmp_buf - this has
 * static scope so will be initialised like this anyway */
parse file /* = { NULL, NULL, 0, fFalse, NULL } */ ;

bool f_export_ok;

static real value[Fr - 1];
#define VAL(N) value[(N)-1]
static real variance[Fr - 1];
#define VAR(N) variance[(N)-1]
static long location[Fr - 1];
#define LOC(N) location[(N)-1]
static int location_width[Fr - 1];
#define WID(N) location_width[(N)-1]

/* style functions */
static void data_normal(void);
static void data_cartesian(void);
static void data_passage(void);
static void data_nosurvey(void);
static void data_ignore(void);

void
get_pos(filepos *fp)
{
   fp->ch = ch;
   fp->offset = ftell(file.fh);
   if (fp->offset == -1)
      fatalerror_in_file(file.filename, 0, /*Error reading file*/18);
}

void
set_pos(const filepos *fp)
{
   ch = fp->ch;
   if (fseek(file.fh, fp->offset, SEEK_SET) == -1)
      fatalerror_in_file(file.filename, 0, /*Error reading file*/18);
}

static void
report_parent(parse * p) {
    if (p->parent)
	report_parent(p->parent);
    /* Force re-report of include tree for further errors in
     * parent files */
    p->reported_where = fFalse;
    /* TRANSLATORS: %s is replaced by the filename of the parent file, and %u
     * by the line number in that file.  Your translation should also contain
     * %s:%u so that automatic parsing of error messages to determine the file
     * and line number still works. */
    fprintf(STDERR, msg(/*In file included from %s:%u:\n*/5), p->filename, p->line);
}

static void
error_list_parent_files(void)
{
   if (!file.reported_where && file.parent) {
      report_parent(file.parent);
      /* Suppress reporting of full include tree for further errors
       * in this file */
      file.reported_where = fTrue;
   }
}

static void
show_line(int col, int width)
{
   /* Rewind to beginning of line. */
   long cur_pos = ftell(file.fh);
   int tabs = 0;
   if (cur_pos < 0 || fseek(file.fh, file.lpos, SEEK_SET) == -1)
      fatalerror_in_file(file.filename, 0, /*Error reading file*/18);

   /* Read the whole line and write it out. */
   PUTC(' ', STDERR);
   while (1) {
      int c = GETC(file.fh);
      /* Note: isEol() is true for EOF */
      if (isEol(c)) break;
      if (c == '\t') ++tabs;
      PUTC(c, STDERR);
   }
   fputnl(STDERR);

   /* If we have a location in the line for the error, indicate it. */
   if (col) {
      PUTC(' ', STDERR);
      if (tabs == 0) {
	 while (--col) PUTC(' ', STDERR);
      } else {
	 /* Copy tabs from line, replacing other characters with spaces - this
	  * means that the caret should line up correctly. */
	 if (fseek(file.fh, file.lpos, SEEK_SET) == -1)
	    fatalerror_in_file(file.filename, 0, /*Error reading file*/18);
	 while (--col) {
	    int c = GETC(file.fh);
	    if (c != '\t') c = ' ';
	    PUTC(c, STDERR);
	 }
      }
      PUTC('^', STDERR);
      while (width > 1) {
	 PUTC('~', STDERR);
	 --width;
      }
      fputnl(STDERR);
   }

   /* Revert to where we were. */
   if (fseek(file.fh, cur_pos, SEEK_SET) == -1)
      fatalerror_in_file(file.filename, 0, /*Error reading file*/18);
}

static int caret_width = 0;

static void
compile_v_report_fpos(int severity, long fpos, int en, va_list ap)
{
   int col = 0;
   error_list_parent_files();
   if (fpos >= file.lpos)
      col = fpos - file.lpos - caret_width;
   v_report(severity, file.filename, file.line, col, en, ap);
   if (file.fh) show_line(col, caret_width);
}

static void
compile_v_report(int diag_flags, int en, va_list ap)
{
   int severity = (diag_flags & DIAG_SEVERITY_MASK);
   if (diag_flags & (DIAG_COL|DIAG_BUF)) {
      if (file.fh) {
	 if (diag_flags & DIAG_BUF) caret_width = strlen(buffer);
	 compile_v_report_fpos(severity, ftell(file.fh), en, ap);
	 if (diag_flags & DIAG_BUF) caret_width = 0;
	 if (diag_flags & DIAG_SKIP) skipline();
	 return;
      }
   }
   error_list_parent_files();
   v_report(severity, file.filename, file.line, 0, en, ap);
   if (file.fh) {
      if (diag_flags & DIAG_BUF) {
	 show_line(0, strlen(buffer));
      } else {
	 show_line(0, caret_width);
      }
   }
   if (diag_flags & DIAG_SKIP) skipline();
}

void
compile_diagnostic(int diag_flags, int en, ...)
{
   va_list ap;
   va_start(ap, en);
   if (diag_flags & (DIAG_TOKEN|DIAG_UINT|DIAG_DATE|DIAG_NUM)) {
      char *p = NULL;
      int len = 0;
      skipblanks();
      if (diag_flags & DIAG_TOKEN) {
	 while (!isBlank(ch) && !isEol(ch)) {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
      } else if (diag_flags & DIAG_UINT) {
	 while (isdigit(ch)) {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
      } else if (diag_flags & DIAG_DATE) {
	 while (isdigit(ch) || ch == '.') {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
      } else {
	 if (isMinus(ch) || isPlus(ch)) {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
	 while (isdigit(ch)) {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
	 if (isDecimal(ch)) {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
	 while (isdigit(ch)) {
	    s_catchar(&p, &len, (char)ch);
	    nextch();
	 }
      }
      if (p) {
	 caret_width = strlen(p);
	 osfree(p);
      }
      compile_v_report(diag_flags|DIAG_COL, en, ap);
      caret_width = 0;
   } else {
      compile_v_report(diag_flags, en, ap);
   }
   va_end(ap);
}

static void
compile_diagnostic_reading(int diag_flags, reading r, int en, ...)
{
   va_list ap;
   int severity = (diag_flags & DIAG_SEVERITY_MASK);
   va_start(ap, en);
   caret_width = WID(r);
   compile_v_report_fpos(severity, LOC(r) + caret_width, en, ap);
   caret_width = 0;
   va_end(ap);
}

static void
compile_error_reading_skip(reading r, int en, ...)
{
   va_list ap;
   va_start(ap, en);
   caret_width = WID(r);
   compile_v_report_fpos(1, LOC(r) + caret_width, en, ap);
   caret_width = 0;
   va_end(ap);
   skipline();
}

void
compile_diagnostic_at(int diag_flags, const char * filename, unsigned line, int en, ...)
{
   va_list ap;
   int severity = (diag_flags & DIAG_SEVERITY_MASK);
   va_start(ap, en);
   v_report(severity, filename, line, 0, en, ap);
   va_end(ap);
}

void
compile_diagnostic_pfx(int diag_flags, const prefix * pfx, int en, ...)
{
   va_list ap;
   int severity = (diag_flags & DIAG_SEVERITY_MASK);
   va_start(ap, en);
   v_report(severity, pfx->filename, pfx->line, 0, en, ap);
   va_end(ap);
}

void
compile_diagnostic_token_show(int diag_flags, int en)
{
   char *p = NULL;
   int len = 0;
   skipblanks();
   while (!isBlank(ch) && !isEol(ch)) {
      s_catchar(&p, &len, (char)ch);
      nextch();
   }
   if (p) {
      caret_width = strlen(p);
      compile_diagnostic(diag_flags|DIAG_COL, en, p);
      caret_width = 0;
      osfree(p);
   } else {
      compile_diagnostic(DIAG_ERR|DIAG_COL, en, "");
   }
}

static void
compile_error_string(const char * s, int en, ...)
{
   va_list ap;
   va_start(ap, en);
   caret_width = strlen(s);
   compile_v_report(DIAG_ERR|DIAG_COL, en, ap);
   va_end(ap);
   caret_width = 0;
}

/* This function makes a note where to put output files */
static void
using_data_file(const char *fnm)
{
   if (!fnm_output_base) {
      /* was: fnm_output_base = base_from_fnm(fnm); */
      fnm_output_base = baseleaf_from_fnm(fnm);
   } else if (fnm_output_base_is_dir) {
      /* --output pointed to directory so use the leaf basename in that dir */
      char *lf, *p;
      lf = baseleaf_from_fnm(fnm);
      p = use_path(fnm_output_base, lf);
      osfree(lf);
      osfree(fnm_output_base);
      fnm_output_base = p;
      fnm_output_base_is_dir = 0;
   }
}

static void
skipword(void)
{
   while (!isBlank(ch) && !isEol(ch)) nextch();
}

extern void
skipblanks(void)
{
   while (isBlank(ch)) nextch();
}

extern void
skipline(void)
{
   while (!isEol(ch)) nextch();
}

static void
process_eol(void)
{
   int eolchar;

   skipblanks();

   if (!isEol(ch)) {
      if (!isComm(ch))
	 compile_diagnostic(DIAG_ERR|DIAG_COL, /*End of line not blank*/15);
      skipline();
   }

   eolchar = ch;
   file.line++;
   /* skip any different eol characters so we get line counts correct on
    * DOS text files and similar, but don't count several adjacent blank
    * lines as one */
   while (ch != EOF) {
      nextch();
      if (ch == eolchar || !isEol(ch)) {
	 break;
      }
      if (ch == '\n') eolchar = ch;
   }
   file.lpos = ftell(file.fh) - 1;
}

static bool
process_non_data_line(void)
{
   skipblanks();

   if (isData(ch)) return fFalse;

   if (isKeywd(ch)) {
      nextch();
      handle_command();
   }

   process_eol();

   return fTrue;
}

static void
read_reading(reading r, bool f_optional)
{
   int n_readings;
   q_quantity q;
   switch (r) {
      case Tape: q = Q_LENGTH; break;
      case BackTape: q = Q_BACKLENGTH; break;
      case Comp: q = Q_BEARING; break;
      case BackComp: q = Q_BACKBEARING; break;
      case Clino: q = Q_GRADIENT; break;
      case BackClino: q = Q_BACKGRADIENT; break;
      case FrDepth: case ToDepth: q = Q_DEPTH; break;
      case Dx: q = Q_DX; break;
      case Dy: q = Q_DY; break;
      case Dz: q = Q_DZ; break;
      case FrCount: case ToCount: q = Q_COUNT; break;
      case Left: q = Q_LEFT; break;
      case Right: q = Q_RIGHT; break;
      case Up: q = Q_UP; break;
      case Down: q = Q_DOWN; break;
      default:
	q = Q_NULL; /* Suppress compiler warning */;
	BUG("Unexpected case");
   }
   LOC(r) = ftell(file.fh);
   /* since we don't handle bearings in read_readings, it's never quadrant */
   VAL(r) = read_numeric_multi(f_optional, fFalse, &n_readings);
   WID(r) = ftell(file.fh) - LOC(r);
   VAR(r) = var(q);
   if (n_readings > 1) VAR(r) /= sqrt(n_readings);
}

static void
read_bearing_or_omit(reading r)
{
   int n_readings;
   bool quadrants = fFalse;
   q_quantity q = Q_NULL;
   switch (r) {
      case Comp:
	q = Q_BEARING;
	if (pcs->f_bearing_quadrants)
	   quadrants = fTrue;
	break;
      case BackComp:
	q = Q_BACKBEARING;
	if (pcs->f_backbearing_quadrants)
	   quadrants = fTrue;
	break;
      default:
	q = Q_NULL; /* Suppress compiler warning */;
	BUG("Unexpected case");
   }
   LOC(r) = ftell(file.fh);
   VAL(r) = read_bearing_multi_or_omit(quadrants, &n_readings);
   WID(r) = ftell(file.fh) - LOC(r);
   VAR(r) = var(q);
   if (n_readings > 1) VAR(r) /= sqrt(n_readings);
}

/* For reading Compass MAK files which have a freeform syntax */
static void
nextch_handling_eol(void)
{
   nextch();
   while (ch != EOF && isEol(ch)) {
      process_eol();
   }
}

#define LITLEN(S) (sizeof(S"") - 1)
#define has_ext(F,L,E) ((L) > LITLEN(E) + 1 &&\
			(F)[(L) - LITLEN(E) - 1] == FNM_SEP_EXT &&\
			strcasecmp((F) + (L) - LITLEN(E), E) == 0)
extern void
data_file(const char *pth, const char *fnm)
{
   int begin_lineno_store;
   parse file_store;
   volatile enum {FMT_SVX, FMT_DAT, FMT_MAK} fmt = FMT_SVX;

   {
      char *filename;
      FILE *fh;
      size_t len;

      if (!pth) {
	 /* file specified on command line - don't do special translation */
	 fh = fopenWithPthAndExt(pth, fnm, EXT_SVX_DATA, "rb", &filename);
      } else {
	 fh = fopen_portable(pth, fnm, EXT_SVX_DATA, "rb", &filename);
      }

      if (fh == NULL) {
	 compile_error_string(fnm, /*Couldn’t open file “%s”*/24, fnm);
	 return;
      }

      len = strlen(filename);
      if (has_ext(filename, len, "dat")) {
	 fmt = FMT_DAT;
      } else if (has_ext(filename, len, "mak")) {
	 fmt = FMT_MAK;
      }

      file_store = file;
      if (file.fh) file.parent = &file_store;
      file.fh = fh;
      file.filename = filename;
      file.line = 1;
      file.lpos = 0;
      file.reported_where = fFalse;
      nextch();
      if (fmt == FMT_SVX && ch == 0xef) {
	 /* Maybe a UTF-8 "BOM" - skip if so. */
	 if (nextch() == 0xbb && nextch() == 0xbf) {
	    nextch();
	    file.lpos = 3;
	 } else {
	    rewind(fh);
	    ch = 0xef;
	 }
      }
   }

   using_data_file(file.filename);

   begin_lineno_store = pcs->begin_lineno;
   pcs->begin_lineno = 0;

   if (fmt == FMT_DAT) {
      short *t;
      int i;
      settings *pcsNew;

      pcsNew = osnew(settings);
      *pcsNew = *pcs; /* copy contents */
      pcsNew->begin_lineno = 0;
      pcsNew->next = pcs;
      pcs = pcsNew;
      default_units(pcs);
      default_calib(pcs);

      pcs->style = STYLE_NORMAL;
      pcs->units[Q_LENGTH] = METRES_PER_FOOT;
      t = ((short*)osmalloc(ossizeof(short) * 257)) + 1;

      t[EOF] = SPECIAL_EOL;
      memset(t, 0, sizeof(short) * 33);
      for (i = 33; i < 127; i++) t[i] = SPECIAL_NAMES;
      t[127] = 0;
      for (i = 128; i < 256; i++) t[i] = SPECIAL_NAMES;
      t['\t'] |= SPECIAL_BLANK;
      t[' '] |= SPECIAL_BLANK;
      t['\032'] |= SPECIAL_EOL; /* Ctrl-Z, so olde DOS text files are handled ok */
      t['\n'] |= SPECIAL_EOL;
      t['\r'] |= SPECIAL_EOL;
      t['.'] |= SPECIAL_DECIMAL;
      t['-'] |= SPECIAL_MINUS;
      t['+'] |= SPECIAL_PLUS;
      pcs->Translate = t;
      pcs->Case = OFF;
      pcs->Truncate = INT_MAX;
      pcs->infer = BIT(INFER_EQUATES)|BIT(INFER_EXPORTS)|BIT(INFER_PLUMBS);
   } else if (fmt == FMT_MAK) {
      short *t;
      int i;
      settings *pcsNew;

      pcsNew = osnew(settings);
      *pcsNew = *pcs; /* copy contents */
      pcsNew->begin_lineno = 0;
      pcsNew->next = pcs;
      pcs = pcsNew;

      t = ((short*)osmalloc(ossizeof(short) * 257)) + 1;

      t[EOF] = SPECIAL_EOL;
      memset(t, 0, sizeof(short) * 33);
      for (i = 33; i < 127; i++) t[i] = SPECIAL_NAMES;
      t[127] = 0;
      for (i = 128; i < 256; i++) t[i] = SPECIAL_NAMES;
      t['['] = t[','] = t[';'] = 0;
      t['\t'] |= SPECIAL_BLANK;
      t[' '] |= SPECIAL_BLANK;
      t['\032'] |= SPECIAL_EOL; /* Ctrl-Z, so olde DOS text files are handled ok */
      t['\n'] |= SPECIAL_EOL;
      t['\r'] |= SPECIAL_EOL;
      t['.'] |= SPECIAL_DECIMAL;
      t['-'] |= SPECIAL_MINUS;
      t['+'] |= SPECIAL_PLUS;
      pcs->Translate = t;
      pcs->Case = OFF;
      pcs->Truncate = INT_MAX;
   }

#ifdef HAVE_SETJMP_H
   /* errors in nested functions can longjmp here */
   if (setjmp(file.jbSkipLine)) {
      skipline();
      process_eol();
   }
#endif

   if (fmt == FMT_DAT) {
      while (ch != EOF && !ferror(file.fh)) {
	 static const reading compass_order[] = {
	    Fr, To, Tape, CompassDATComp, CompassDATClino,
	    CompassDATLeft, CompassDATRight, CompassDATUp, CompassDATDown,
	    CompassDATFlags, IgnoreAll
	 };
	 static const reading compass_order_backsights[] = {
	    Fr, To, Tape, CompassDATComp, CompassDATClino,
	    CompassDATLeft, CompassDATRight, CompassDATUp, CompassDATDown,
	    CompassDATBackComp, CompassDATBackClino,
	    CompassDATFlags, IgnoreAll
	 };
	 /* <Cave name> */
	 skipline();
	 process_eol();
	 /* SURVEY NAME: <Short name> */
	 get_token();
	 get_token();
	 /* if (ch != ':') ... */
	 nextch();
	 get_token();
	 skipline();
	 process_eol();
	 /* SURVEY DATE: 7 10 79  COMMENT:<Long name> */
	 get_token();
	 get_token();
	 copy_on_write_meta(pcs);
	 if (ch == ':') {
	     int year, month, day;

	     nextch();

	     /* NB order is *month* *day* year */
	     month = read_uint();
	     day = read_uint();
	     year = read_uint();
	     /* Note: Larry says a 2 digit year is always 19XX */
	     if (year < 100) year += 1900;

	     pcs->meta->days1 = pcs->meta->days2 = days_since_1900(year, month, day);
	 } else {
	     pcs->meta->days1 = pcs->meta->days2 = -1;
	 }
	 pcs->declination = HUGE_REAL;
	 skipline();
	 process_eol();
	 /* SURVEY TEAM: */
	 get_token();
	 get_token();
	 skipline();
	 process_eol();
	 /* <Survey team> */
	 skipline();
	 process_eol();
	 /* DECLINATION: 1.00  FORMAT: DDDDLUDRADLN  CORRECTIONS: 2.00 3.00 4.00 */
	 get_token();
	 nextch(); /* : */
	 skipblanks();
	 pcs->z[Q_DECLINATION] = -read_numeric(fFalse);
	 pcs->z[Q_DECLINATION] *= pcs->units[Q_DECLINATION];
	 get_token();
	 pcs->ordering = compass_order;
	 if (strcmp(buffer, "FORMAT") == 0) {
	    nextch(); /* : */
	    get_token();
	    if (strlen(buffer) >= 12 && buffer[11] == 'B') {
	       /* We have backsights for compass and clino */
	       pcs->ordering = compass_order_backsights;
	    }
	    get_token();
	 }
	 if (strcmp(buffer, "CORRECTIONS") == 0) {
	    nextch(); /* : */
	    pcs->z[Q_BEARING] = -rad(read_numeric(fFalse));
	    pcs->z[Q_GRADIENT] = -rad(read_numeric(fFalse));
	    pcs->z[Q_LENGTH] = -read_numeric(fFalse);
	 } else {
	    pcs->z[Q_BEARING] = 0;
	    pcs->z[Q_GRADIENT] = 0;
	    pcs->z[Q_LENGTH] = 0;
	 }
	 skipline();
	 process_eol();
	 /* BLANK LINE */
	 skipline();
	 process_eol();
	 /* heading line */
	 skipline();
	 process_eol();
	 /* BLANK LINE */
	 skipline();
	 process_eol();
	 while (ch != EOF) {
	    if (ch == '\x0c') {
	       nextch();
	       process_eol();
	       break;
	    }
	    data_normal();
	 }
	 clear_last_leg();
      }
      {
	 settings *pcsParent = pcs->next;
	 SVX_ASSERT(pcsParent);
	 pcs->ordering = NULL;
	 free_settings(pcs);
	 pcs = pcsParent;
      }
   } else if (fmt == FMT_MAK) {
      while (ch != EOF && !ferror(file.fh)) {
	 if (ch == '#') {
	    /* include a file */
	    int ch_store;
	    char *dat_pth = path_from_fnm(file.filename);
	    char *dat_fnm = NULL;
	    int dat_fnm_len;
	    nextch_handling_eol();
	    while (ch != ',' && ch != ';' && ch != EOF) {
	       while (isEol(ch)) process_eol();
	       s_catchar(&dat_fnm, &dat_fnm_len, (char)ch);
	       nextch_handling_eol();
	    }
	    while (ch != ';' && ch != EOF) {
	       prefix *name;
	       nextch_handling_eol();
	       name = read_prefix(PFX_STATION|PFX_OPT);
	       if (name) {
		  skipblanks();
		  if (ch == '[') {
		     /* fixed pt */
		     node *stn;
		     real x, y, z;
		     bool in_feet = fFalse;
		     name->sflags |= BIT(SFLAGS_FIXED);
		     nextch_handling_eol();
		     if (ch == 'F' || ch == 'f') {
			in_feet = fTrue;
			nextch_handling_eol();
		     } else if (ch == 'M' || ch == 'm') {
			nextch_handling_eol();
		     } else {
			compile_diagnostic(DIAG_ERR, /*Expecting “F” or “M”*/103);
		     }
		     while (!isdigit(ch) && ch != '+' && ch != '-' &&
			    ch != '.' && ch != ']' && ch != EOF) {
			nextch_handling_eol();
		     }
		     x = read_numeric(fFalse);
		     while (!isdigit(ch) && ch != '+' && ch != '-' &&
			    ch != '.' && ch != ']' && ch != EOF) {
			nextch_handling_eol();
		     }
		     y = read_numeric(fFalse);
		     while (!isdigit(ch) && ch != '+' && ch != '-' &&
			    ch != '.' && ch != ']' && ch != EOF) {
			nextch_handling_eol();
		     }
		     z = read_numeric(fFalse);
		     if (in_feet) {
			x *= METRES_PER_FOOT;
			y *= METRES_PER_FOOT;
			z *= METRES_PER_FOOT;
		     }
		     stn = StnFromPfx(name);
		     if (!fixed(stn)) {
			POS(stn, 0) = x;
			POS(stn, 1) = y;
			POS(stn, 2) = z;
			fix(stn);
		     } else {
			if (x != POS(stn, 0) || y != POS(stn, 1) ||
			    z != POS(stn, 2)) {
			   compile_diagnostic(DIAG_ERR, /*Station already fixed or equated to a fixed point*/46);
			} else {
			   compile_diagnostic(DIAG_WARN, /*Station already fixed at the same coordinates*/55);
			}
		     }
		     while (ch != ']' && ch != EOF) nextch_handling_eol();
		     if (ch == ']') {
			nextch_handling_eol();
			skipblanks();
		     }
		  } else {
		     /* FIXME: link station - ignore for now */
		     /* FIXME: perhaps issue warning?  Other station names can be "reused", which is problematic... */
		  }
		  while (ch != ',' && ch != ';' && ch != EOF)
		     nextch_handling_eol();
	       }
	    }
	    if (dat_fnm) {
	       ch_store = ch;
	       data_file(dat_pth, dat_fnm);
	       ch = ch_store;
	       osfree(dat_fnm);
	    }
	 } else {
	    /* FIXME: also check for % and $ later */
	    nextch_handling_eol();
	 }
      }
      {
	 settings *pcsParent = pcs->next;
	 SVX_ASSERT(pcsParent);
	 free_settings(pcs);
	 pcs = pcsParent;
      }
   } else {
      while (ch != EOF && !ferror(file.fh)) {
	 if (!process_non_data_line()) {
	    f_export_ok = fFalse;
	    switch (pcs->style) {
	     case STYLE_NORMAL:
	     case STYLE_DIVING:
	     case STYLE_CYLPOLAR:
	       data_normal();
	       break;
	     case STYLE_CARTESIAN:
	       data_cartesian();
	       break;
	     case STYLE_PASSAGE:
	       data_passage();
	       break;
	     case STYLE_NOSURVEY:
	       data_nosurvey();
	       break;
	     case STYLE_IGNORE:
	       data_ignore();
	       break;
	     default:
	       BUG("bad style");
	    }
	 }
      }
      clear_last_leg();
   }

   /* don't allow *BEGIN at the end of a file, then *EXPORT in the
    * including file */
   f_export_ok = fFalse;

   if (pcs->begin_lineno) {
      error_in_file(file.filename, pcs->begin_lineno,
		    /*BEGIN with no matching END in this file*/23);
      /* Implicitly close any unclosed BEGINs from this file */
      do {
	 settings *pcsParent = pcs->next;
	 SVX_ASSERT(pcsParent);
	 free_settings(pcs);
	 pcs = pcsParent;
      } while (pcs->begin_lineno);
   }

   pcs->begin_lineno = begin_lineno_store;

   if (ferror(file.fh))
      fatalerror_in_file(file.filename, 0, /*Error reading file*/18);

   (void)fclose(file.fh);

   file = file_store;

   /* don't free this - it may be pointed to by prefix.file */
   /* osfree(file.filename); */
}

static real
mod2pi(real a)
{
   return a - floor(a / (2 * M_PI)) * (2 * M_PI);
}

static real
handle_plumb(clino_type *p_ctype)
{
   typedef enum {
      CLINO_NULL=-1, CLINO_UP, CLINO_DOWN, CLINO_LEVEL
   } clino_tok;
   static const sztok clino_tab[] = {
      {"D",     CLINO_DOWN},
      {"DOWN",  CLINO_DOWN},
      {"H",     CLINO_LEVEL},
      {"LEVEL", CLINO_LEVEL},
      {"U",     CLINO_UP},
      {"UP",    CLINO_UP},
      {NULL,    CLINO_NULL}
   };
   static const real clinos[] = {(real)M_PI_2, (real)(-M_PI_2), (real)0.0};
   clino_tok tok;

   skipblanks();
   if (isalpha(ch)) {
      filepos fp;
      get_pos(&fp);
      get_token();
      tok = match_tok(clino_tab, TABSIZE(clino_tab));
      if (tok != CLINO_NULL) {
	 *p_ctype = (tok == CLINO_LEVEL ? CTYPE_HORIZ : CTYPE_PLUMB);
	 return clinos[tok];
      }
      set_pos(&fp);
   } else if (isSign(ch)) {
      int chOld = ch;
      nextch();
      if (toupper(ch) == 'V') {
	 nextch();
	 *p_ctype = CTYPE_PLUMB;
	 return (!isMinus(chOld) ? M_PI_2 : -M_PI_2);
      }

      if (isOmit(chOld)) {
	 *p_ctype = CTYPE_OMIT;
	 /* no clino reading, so assume 0 with large sd */
	 return (real)0.0;
      }
   } else if (isOmit(ch)) {
      /* OMIT char may not be a SIGN char too so we need to check here as
       * well as above... */
      nextch();
      *p_ctype = CTYPE_OMIT;
      /* no clino reading, so assume 0 with large sd */
      return (real)0.0;
   }
   return HUGE_REAL;
}

static void
warn_readings_differ(int msgno, real diff, int units)
{
   char buf[64];
   char *p;
   diff /= get_units_factor(units);
   sprintf(buf, "%.2f", fabs(diff));
   for (p = buf; *p; ++p) {
      if (*p == '.') {
	 char *z = p;
	 while (*++p) {
	    if (*p != '0') z = p + 1;
	 }
	 p = z;
	 break;
      }
   }
   strcpy(p, get_units_string(units));
   compile_diagnostic(DIAG_WARN, msgno, buf);
}

static bool
handle_comp_units(void)
{
   bool fNoComp = fTrue;
   if (VAL(Comp) != HUGE_REAL) {
      fNoComp = fFalse;
      VAL(Comp) *= pcs->units[Q_BEARING];
      if (VAL(Comp) < (real)0.0 || VAL(Comp) - M_PI * 2.0 > EPSILON) {
	 /* TRANSLATORS: Suspicious means something like 410 degrees or -20
	  * degrees */
	 compile_diagnostic_reading(DIAG_WARN, Comp, /*Suspicious compass reading*/59);
	 VAL(Comp) = mod2pi(VAL(Comp));
      }
   }
   if (VAL(BackComp) != HUGE_REAL) {
      fNoComp = fFalse;
      VAL(BackComp) *= pcs->units[Q_BACKBEARING];
      if (VAL(BackComp) < (real)0.0 || VAL(BackComp) - M_PI * 2.0 > EPSILON) {
	 /* FIXME: different message for BackComp? */
	 compile_diagnostic_reading(DIAG_WARN, BackComp, /*Suspicious compass reading*/59);
	 VAL(BackComp) = mod2pi(VAL(BackComp));
      }
   }
   return fNoComp;
}

static real
handle_compass(real *p_var)
{
   real compvar = VAR(Comp);
   real comp = VAL(Comp);
   real backcomp = VAL(BackComp);
   real declination;
   if (pcs->z[Q_DECLINATION] != HUGE_REAL) {
      declination = -pcs->z[Q_DECLINATION];
   } else if (pcs->declination != HUGE_REAL) {
      /* Cached value calculated for a previous compass reading taken on the
       * same date (by the 'else' just below).
       */
      declination = pcs->declination;
   } else {
      if (!pcs->meta || pcs->meta->days1 == -1) {
	  compile_diagnostic(DIAG_WARN, /*No survey date specified - using 0 for magnetic declination*/304);
	  declination = 0;
      } else {
	  int avg_days = (pcs->meta->days1 + pcs->meta->days2) / 2;
	  double dat = julian_date_from_days_since_1900(avg_days);
	  /* thgeomag() takes (lat, lon, h, dat) - i.e. (y, x, z, date). */
	  declination = thgeomag(pcs->dec_lat, pcs->dec_lon, pcs->dec_alt, dat);
      }
      declination -= pcs->convergence;
      /* We cache the calculated declination as the calculation is relatively
       * expensive.  We also cache an "assumed 0" answer so that we only
       * warn once per such survey rather than for every line with a compass
       * reading. */
      pcs->declination = declination;
   }
   if (comp != HUGE_REAL) {
      comp = (comp - pcs->z[Q_BEARING]) * pcs->sc[Q_BEARING];
      comp += declination;
   }
   if (backcomp != HUGE_REAL) {
      backcomp = (backcomp - pcs->z[Q_BACKBEARING])
	      * pcs->sc[Q_BACKBEARING];
      backcomp += declination;
      backcomp -= M_PI;
      if (comp != HUGE_REAL) {
	 real diff = comp - backcomp;
	 real adj = fabs(diff) > M_PI ? M_PI : 0;
	 diff -= floor((diff + M_PI) / (2 * M_PI)) * 2 * M_PI;
	 if (sqrd(diff / 3.0) > compvar + VAR(BackComp)) {
	    /* fore and back readings differ by more than 3 sds */
	    /* TRANSLATORS: %s is replaced by the amount the readings disagree
	     * by, e.g. "2.5°" or "3ᵍ". */
	    warn_readings_differ(/*COMPASS reading and BACKCOMPASS reading disagree by %s*/98,
				 diff, get_angle_units(Q_BEARING));
	 }
	 comp = (comp / compvar + backcomp / VAR(BackComp));
	 compvar = (compvar + VAR(BackComp)) / 4;
	 comp *= compvar;
	 comp += adj;
      } else {
	 comp = backcomp;
	 compvar = VAR(BackComp);
      }
   }
   *p_var = compvar;
   return comp;
}

static real
handle_clino(q_quantity q, reading r, real val, bool percent, clino_type *p_ctype)
{
   bool range_0_180;
   real z;
   real diff_from_abs90;
   val *= pcs->units[q];
   /* percentage scale */
   if (percent) val = atan(val);
   /* We want to warn if there's a reading which it would be impossible
    * to have read from the instrument (e.g. on a -90 to 90 degree scale
    * you can't read "96" (it's probably a typo for "69").  However, the
    * gradient reading from a topofil is typically in the range 0 to 180,
    * with 90 being horizontal.
    *
    * Really we should allow the valid range to be specified, but for now
    * we infer it from the zero error - if this is within 45 degrees of
    * 90 then we assume the range is 0 to 180.
    */
   z = pcs->z[q];
   range_0_180 = (z > M_PI_4 && z < 3*M_PI_4);
   diff_from_abs90 = fabs(val) - M_PI_2;
   if (diff_from_abs90 > EPSILON) {
      if (!range_0_180) {
	 int clino_units = get_angle_units(q);
	 const char * units = get_units_string(clino_units);
	 real right_angle = M_PI_2 / get_units_factor(clino_units);
	 /* FIXME: different message for BackClino? */
	 /* TRANSLATORS: %.f%s will be replaced with a right angle in the
	  * units currently in use, e.g. "90°" or "100ᵍ".  And "absolute
	  * value" means the reading ignoring the sign (so it might be
	  * < -90° or > 90°. */
	 compile_diagnostic_reading(DIAG_WARN, r, /*Clino reading over %.f%s (absolute value)*/51,
				    right_angle, units);
      }
   } else if (TSTBIT(pcs->infer, INFER_PLUMBS) &&
	      diff_from_abs90 >= -EPSILON) {
      *p_ctype = CTYPE_INFERPLUMB;
   }
   if (range_0_180 && *p_ctype != CTYPE_INFERPLUMB) {
      /* FIXME: Warning message not ideal... */
      if (val < 0.0 || val - M_PI > EPSILON) {
	 int clino_units = get_angle_units(q);
	 const char * units = get_units_string(clino_units);
	 real right_angle = M_PI_2 / get_units_factor(clino_units);
	 compile_diagnostic_reading(DIAG_WARN, r, /*Clino reading over %.f%s (absolute value)*/51,
				    right_angle, units);
      }
   }
   return val;
}

static int
process_normal(prefix *fr, prefix *to, bool fToFirst,
	       clino_type ctype, clino_type backctype)
{
   real tape = VAL(Tape);
   real clin = VAL(Clino);
   real backclin = VAL(BackClino);

   real dx, dy, dz;
   real vx, vy, vz;
#ifndef NO_COVARIANCES
   real cxy, cyz, czx;
#endif

   bool fNoComp;

   /* adjusted tape is negative -- probably the calibration is wrong */
   if (tape < (real)0.0) {
      /* TRANSLATE different message for topofil? */
      compile_diagnostic_reading(DIAG_WARN, Tape, /*Negative adjusted tape reading*/79);
   }

   fNoComp = handle_comp_units();

   if (ctype == CTYPE_READING) {
      clin = handle_clino(Q_GRADIENT, Clino, clin,
			  pcs->f_clino_percent, &ctype);
   }

   if (backctype == CTYPE_READING) {
      backclin = handle_clino(Q_BACKGRADIENT, BackClino, backclin,
			      pcs->f_backclino_percent, &backctype);
   }

   /* un-infer the plumb if the backsight was just a reading */
   if (ctype == CTYPE_INFERPLUMB && backctype == CTYPE_READING) {
       ctype = CTYPE_READING;
   }

   if (ctype != CTYPE_OMIT && backctype != CTYPE_OMIT && ctype != backctype) {
      /* TRANSLATORS: In data with backsights, the user has tried to give a
       * plumb for the foresight and a clino reading for the backsight, or
       * something similar. */
      compile_error_reading_skip(Clino, /*CLINO and BACKCLINO readings must be of the same type*/84);
      return 0;
   }

   if (ctype == CTYPE_PLUMB || ctype == CTYPE_INFERPLUMB ||
       backctype == CTYPE_PLUMB || backctype == CTYPE_INFERPLUMB) {
      /* plumbed */
      if (!fNoComp) {
	 if (ctype == CTYPE_PLUMB ||
	     (ctype == CTYPE_INFERPLUMB && VAL(Comp) != 0.0) ||
	     backctype == CTYPE_PLUMB ||
	     (backctype == CTYPE_INFERPLUMB && VAL(BackComp) != 0.0)) {
	    /* FIXME: Different message for BackComp? */
	    /* TRANSLATORS: A "plumbed leg" is one measured using a plumbline
	     * (a weight on a string).  So the problem here is that the leg is
	     * vertical, so a compass reading has no meaning! */
	    compile_diagnostic(DIAG_WARN, /*Compass reading given on plumbed leg*/21);
	 }
      }

      dx = dy = (real)0.0;
      if (ctype != CTYPE_OMIT) {
	 if (backctype != CTYPE_OMIT && (clin > 0) == (backclin > 0)) {
	    /* TRANSLATORS: We've been told the foresight and backsight are
	     * both "UP", or that they're both "DOWN". */
	    compile_error_reading_skip(Clino, /*Plumbed CLINO and BACKCLINO readings can't be in the same direction*/92);
	    return 0;
	 }
	 dz = (clin > (real)0.0) ? tape : -tape;
      } else {
	 dz = (backclin < (real)0.0) ? tape : -tape;
      }
      vx = vy = var(Q_POS) / 3.0 + dz * dz * var(Q_PLUMB);
      vz = var(Q_POS) / 3.0 + VAR(Tape);
#ifndef NO_COVARIANCES
      /* Correct values - no covariances in this case! */
      cxy = cyz = czx = (real)0.0;
#endif
   } else {
      /* Each of ctype and backctype are either CTYPE_READING/CTYPE_HORIZ
       * or CTYPE_OMIT */
      /* clino */
      real L2, cosG, LcosG, cosG2, sinB, cosB, dx2, dy2, dz2, v, V;
      if (fNoComp) {
	 /* TRANSLATORS: Here "legs" are survey legs, i.e. measurements between
	  * survey stations. */
	 compile_error_reading_skip(Comp, /*Compass reading may not be omitted except on plumbed legs*/14);
	 return 0;
      }
      if (tape == (real)0.0) {
	 dx = dy = dz = (real)0.0;
	 vx = vy = vz = (real)(var(Q_POS) / 3.0); /* Position error only */
#ifndef NO_COVARIANCES
	 cxy = cyz = czx = (real)0.0;
#endif
#if DEBUG_DATAIN_1
	 printf("Zero length leg: vx = %f, vy = %f, vz = %f\n", vx, vy, vz);
#endif
      } else {
	 real sinGcosG;
	 /* take into account variance in LEVEL case */
	 real var_clin = var(Q_LEVEL);
	 real var_comp;
	 real comp = handle_compass(&var_comp);
	 /* ctype != CTYPE_READING is LEVEL case */
	 if (ctype == CTYPE_READING) {
	    clin = (clin - pcs->z[Q_GRADIENT]) * pcs->sc[Q_GRADIENT];
	    var_clin = VAR(Clino);
	 }
	 if (backctype == CTYPE_READING) {
	    backclin = (backclin - pcs->z[Q_BACKGRADIENT])
	       * pcs->sc[Q_BACKGRADIENT];
	    if (ctype == CTYPE_READING) {
	       if (sqrd((clin + backclin) / 3.0) > var_clin + VAR(BackClino)) {
		  /* fore and back readings differ by more than 3 sds */
		  /* TRANSLATORS: %s is replaced by the amount the readings disagree
		   * by, e.g. "2.5°" or "3ᵍ". */
		  warn_readings_differ(/*CLINO reading and BACKCLINO reading disagree by %s*/99,
				       clin + backclin, get_angle_units(Q_GRADIENT));
	       }
	       clin = (clin / var_clin - backclin / VAR(BackClino));
	       var_clin = (var_clin + VAR(BackClino)) / 4;
	       clin *= var_clin;
	    } else {
	       clin = -backclin;
	       var_clin = VAR(BackClino);
	    }
	 }

#if DEBUG_DATAIN
	 printf("    %4.2f %4.2f %4.2f\n", tape, comp, clin);
#endif
	 cosG = cos(clin);
	 LcosG = tape * cosG;
	 sinB = sin(comp);
	 cosB = cos(comp);
#if DEBUG_DATAIN_1
	 printf("sinB = %f, cosG = %f, LcosG = %f\n", sinB, cosG, LcosG);
#endif
	 dx = LcosG * sinB;
	 dy = LcosG * cosB;
	 dz = tape * sin(clin);
/*      printf("%.2f\n",clin); */
#if DEBUG_DATAIN_1
	 printf("dx = %f\ndy = %f\ndz = %f\n", dx, dy, dz);
#endif
	 dx2 = dx * dx;
	 L2 = tape * tape;
	 V = VAR(Tape) / L2;
	 dy2 = dy * dy;
	 cosG2 = cosG * cosG;
	 sinGcosG = sin(clin) * cosG;
	 dz2 = dz * dz;
	 v = dz2 * var_clin;
#ifdef NO_COVARIANCES
	 vx = (var(Q_POS) / 3.0 + dx2 * V + dy2 * var_comp +
	       (.5 + sinB * sinB * cosG2) * v);
	 vy = (var(Q_POS) / 3.0 + dy2 * V + dx2 * var_comp +
	       (.5 + cosB * cosB * cosG2) * v);
	 if (ctype == CTYPE_OMIT && backctype == CTYPE_OMIT) {
	    /* if no clino, assume sd=tape/sqrt(10) so 3sds = .95*tape */
	    vz = var(Q_POS) / 3.0 + L2 * (real)0.1;
	 } else {
	    vz = var(Q_POS) / 3.0 + dz2 * V + L2 * cosG2 * var_clin;
	 }
	 /* for Surveyor87 errors: vx=vy=vz=var(Q_POS)/3.0; */
#else
	 vx = var(Q_POS) / 3.0 + dx2 * V + dy2 * var_comp +
	    (sinB * sinB * v);
	 vy = var(Q_POS) / 3.0 + dy2 * V + dx2 * var_comp +
	    (cosB * cosB * v);
	 if (ctype == CTYPE_OMIT && backctype == CTYPE_OMIT) {
	    /* if no clino, assume sd=tape/sqrt(10) so 3sds = .95*tape */
	    vz = var(Q_POS) / 3.0 + L2 * (real)0.1;
	 } else {
	    vz = var(Q_POS) / 3.0 + dz2 * V + L2 * cosG2 * var_clin;
	 }
	 /* usual covariance formulae are fine in no clino case since
	  * dz = 0 so value of var_clin is ignored */
	 cxy = sinB * cosB * (VAR(Tape) * cosG2 + var_clin * dz2)
	       - var_comp * dx * dy;
	 czx = VAR(Tape) * sinB * sinGcosG - var_clin * dx * dz;
	 cyz = VAR(Tape) * cosB * sinGcosG - var_clin * dy * dz;
#if 0
	 printf("vx = %6.3f, vy = %6.3f, vz = %6.3f\n", vx, vy, vz);
	 printf("cxy = %6.3f, cyz = %6.3f, czx = %6.3f\n", cxy, cyz, czx);
#endif
#endif
#if DEBUG_DATAIN_1
	 printf("In DATAIN.C, vx = %f, vy = %f, vz = %f\n", vx, vy, vz);
#endif
      }
   }
#if DEBUG_DATAIN_1
   printf("Just before addleg, vx = %f\n", vx);
#endif
   /*printf("dx,dy,dz = %.2f %.2f %.2f\n\n", dx, dy, dz);*/
   addlegbyname(fr, to, fToFirst, dx, dy, dz, vx, vy, vz
#ifndef NO_COVARIANCES
		, cyz, czx, cxy
#endif
		);
   return 1;
}

static int
process_diving(prefix *fr, prefix *to, bool fToFirst, bool fDepthChange)
{
   real tape = VAL(Tape);

   real dx, dy, dz;
   real vx, vy, vz;
#ifndef NO_COVARIANCES
   real cxy = 0, cyz = 0, czx = 0;
#endif

   handle_comp_units();

   /* depth gauge readings increase upwards with default calibration */
   if (fDepthChange) {
      SVX_ASSERT(VAL(FrDepth) == 0.0);
      dz = VAL(ToDepth) * pcs->units[Q_DEPTH] - pcs->z[Q_DEPTH];
      dz *= pcs->sc[Q_DEPTH];
   } else {
      dz = VAL(ToDepth) - VAL(FrDepth);
      dz *= pcs->units[Q_DEPTH] * pcs->sc[Q_DEPTH];
   }

   /* adjusted tape is negative -- probably the calibration is wrong */
   if (tape < (real)0.0) {
      compile_diagnostic(DIAG_WARN, /*Negative adjusted tape reading*/79);
   }

   /* check if tape is less than depth change */
   if (tape < fabs(dz)) {
      /* FIXME: allow margin of error based on variances? */
      /* TRANSLATORS: This means that the data fed in said this.
       *
       * It could be a gross error (e.g. the decimal point is missing from the
       * depth gauge reading) or it could just be due to random error on a near
       * vertical leg */
      compile_diagnostic(DIAG_WARN, /*Tape reading is less than change in depth*/62);
   }

   if (tape == (real)0.0 && dz == 0.0) {
      dx = dy = dz = (real)0.0;
      vx = vy = vz = (real)(var(Q_POS) / 3.0); /* Position error only */
   } else if (VAL(Comp) == HUGE_REAL &&
	      VAL(BackComp) == HUGE_REAL) {
      /* plumb */
      dx = dy = (real)0.0;
      if (dz < 0) tape = -tape;
      /* FIXME: Should use FrDepth sometimes... */
      dz = (dz * VAR(Tape) + tape * 2 * VAR(ToDepth))
	 / (VAR(Tape) * 2 * VAR(ToDepth));
      vx = vy = var(Q_POS) / 3.0 + dz * dz * var(Q_PLUMB);
      /* FIXME: Should use FrDepth sometimes... */
      vz = var(Q_POS) / 3.0 + VAR(Tape) * 2 * VAR(ToDepth)
			      / (VAR(Tape) + VAR(ToDepth));
   } else {
      real L2, sinB, cosB, dz2, D2;
      real var_comp;
      real comp = handle_compass(&var_comp);
      sinB = sin(comp);
      cosB = cos(comp);
      L2 = tape * tape;
      dz2 = dz * dz;
      D2 = L2 - dz2;
      if (D2 <= (real)0.0) {
	 /* FIXME: Should use FrDepth sometimes... */
	 real vsum = VAR(Tape) + 2 * VAR(ToDepth);
	 dx = dy = (real)0.0;
	 vx = vy = var(Q_POS) / 3.0;
	 /* FIXME: Should use FrDepth sometimes... */
	 vz = var(Q_POS) / 3.0 + VAR(Tape) * 2 * VAR(ToDepth) / vsum;
	 if (dz > 0) {
	    /* FIXME: Should use FrDepth sometimes... */
	    dz = (dz * VAR(Tape) + tape * 2 * VAR(ToDepth)) / vsum;
	 } else {
	    dz = (dz * VAR(Tape) - tape * 2 * VAR(ToDepth)) / vsum;
	 }
      } else {
	 real D = sqrt(D2);
	 /* FIXME: Should use FrDepth sometimes... */
	 real F = VAR(Tape) * L2 + 2 * VAR(ToDepth) * D2;
	 dx = D * sinB;
	 dy = D * cosB;

	 vx = var(Q_POS) / 3.0 +
	    sinB * sinB * F / D2 + var_comp * dy * dy;
	 vy = var(Q_POS) / 3.0 +
	    cosB * cosB * F / D2 + var_comp * dx * dx;
	 /* FIXME: Should use FrDepth sometimes... */
	 vz = var(Q_POS) / 3.0 + 2 * VAR(ToDepth);

#ifndef NO_COVARIANCES
	 cxy = sinB * cosB * (F / D2 + var_comp * D2);
	 /* FIXME: Should use FrDepth sometimes... */
	 cyz = -2 * VAR(ToDepth) * dy / D;
	 czx = -2 * VAR(ToDepth) * dx / D;
#endif
      }
      /* FIXME: If there's a clino reading, check it against the depth reading,
       * and average.
       * if (VAL(Clino) != HUGE_REAL || VAL(BackClino) != HUGE_REAL) { ... }
       */
   }
   addlegbyname(fr, to, fToFirst, dx, dy, dz, vx, vy, vz
#ifndef NO_COVARIANCES
		, cxy, cyz, czx
#endif
		);
   return 1;
}

static int
process_cartesian(prefix *fr, prefix *to, bool fToFirst)
{
   real dx = (VAL(Dx) * pcs->units[Q_DX] - pcs->z[Q_DX]) * pcs->sc[Q_DX];
   real dy = (VAL(Dy) * pcs->units[Q_DY] - pcs->z[Q_DY]) * pcs->sc[Q_DY];
   real dz = (VAL(Dz) * pcs->units[Q_DZ] - pcs->z[Q_DZ]) * pcs->sc[Q_DZ];

   addlegbyname(fr, to, fToFirst, dx, dy, dz, VAR(Dx), VAR(Dy), VAR(Dz)
#ifndef NO_COVARIANCES
		, 0, 0, 0
#endif
		);
   return 1;
}

static void
data_cartesian(void)
{
   prefix *fr = NULL, *to = NULL;

   bool fMulti = fFalse;

   reading first_stn = End;

   const reading *ordering;

   again:

   for (ordering = pcs->ordering ; ; ordering++) {
      skipblanks();
      switch (*ordering) {
       case Fr:
	 fr = read_prefix(PFX_STATION|PFX_ALLOW_ROOT);
	 if (first_stn == End) first_stn = Fr;
	 break;
       case To:
	 to = read_prefix(PFX_STATION|PFX_ALLOW_ROOT);
	 if (first_stn == End) first_stn = To;
	 break;
       case Station:
	 fr = to;
	 to = read_prefix(PFX_STATION);
	 first_stn = To;
	 break;
       case Dx: case Dy: case Dz:
	 read_reading(*ordering, fFalse);
	 break;
       case Ignore:
	 skipword(); break;
       case IgnoreAllAndNewLine:
	 skipline();
	 /* fall through */
       case Newline:
	 if (fr != NULL) {
	    if (!process_cartesian(fr, to, first_stn == To))
	       skipline();
	 }
	 fMulti = fTrue;
	 while (1) {
	    process_eol();
	    skipblanks();
	    if (isData(ch)) break;
	    if (!isComm(ch)) {
	       return;
	    }
	 }
	 break;
       case IgnoreAll:
	 skipline();
	 /* fall through */
       case End:
	 if (!fMulti) {
	    process_cartesian(fr, to, first_stn == To);
	    process_eol();
	    return;
	 }
	 do {
	    process_eol();
	    skipblanks();
	 } while (isComm(ch));
	 goto again;
       default: BUG("Unknown reading in ordering");
      }
   }
}

static int
process_cylpolar(prefix *fr, prefix *to, bool fToFirst, bool fDepthChange)
{
   real tape = VAL(Tape);

   real dx, dy, dz;
   real vx, vy, vz;
#ifndef NO_COVARIANCES
   real cxy = 0;
#endif

   handle_comp_units();

   /* depth gauge readings increase upwards with default calibration */
   if (fDepthChange) {
      SVX_ASSERT(VAL(FrDepth) == 0.0);
      dz = VAL(ToDepth) * pcs->units[Q_DEPTH] - pcs->z[Q_DEPTH];
      dz *= pcs->sc[Q_DEPTH];
   } else {
      dz = VAL(ToDepth) - VAL(FrDepth);
      dz *= pcs->units[Q_DEPTH] * pcs->sc[Q_DEPTH];
   }

   /* adjusted tape is negative -- probably the calibration is wrong */
   if (tape < (real)0.0) {
      compile_diagnostic(DIAG_WARN, /*Negative adjusted tape reading*/79);
   }

   if (VAL(Comp) == HUGE_REAL && VAL(BackComp) == HUGE_REAL) {
      /* plumb */
      dx = dy = (real)0.0;
      vx = vy = var(Q_POS) / 3.0 + dz * dz * var(Q_PLUMB);
      /* FIXME: Should use FrDepth sometimes... */
      vz = var(Q_POS) / 3.0 + 2 * VAR(ToDepth);
   } else {
      real sinB, cosB;
      real var_comp;
      real comp = handle_compass(&var_comp);
      sinB = sin(comp);
      cosB = cos(comp);

      dx = tape * sinB;
      dy = tape * cosB;

      vx = var(Q_POS) / 3.0 +
	 VAR(Tape) * sinB * sinB + var_comp * dy * dy;
      vy = var(Q_POS) / 3.0 +
	 VAR(Tape) * cosB * cosB + var_comp * dx * dx;
      /* FIXME: Should use FrDepth sometimes... */
      vz = var(Q_POS) / 3.0 + 2 * VAR(ToDepth);

#ifndef NO_COVARIANCES
      cxy = (VAR(Tape) - var_comp * tape * tape) * sinB * cosB;
#endif
   }
   addlegbyname(fr, to, fToFirst, dx, dy, dz, vx, vy, vz
#ifndef NO_COVARIANCES
		, cxy, 0, 0
#endif
		);
   return 1;
}

/* Process tape/compass/clino, diving, and cylpolar styles of survey data
 * Also handles topofil (fromcount/tocount or count) in place of tape */
static void
data_normal(void)
{
   prefix *fr = NULL, *to = NULL;
   reading first_stn = End;

   bool fTopofil = fFalse, fMulti = fFalse;
   bool fRev;
   clino_type ctype, backctype;
   bool fDepthChange;
   unsigned long compass_dat_flags = 0;

   const reading *ordering;

   VAL(Tape) = VAL(BackTape) = HUGE_REAL;
   VAL(Comp) = VAL(BackComp) = HUGE_REAL;
   VAL(FrCount) = VAL(ToCount) = 0;
   VAL(FrDepth) = VAL(ToDepth) = 0;
   VAL(Left) = VAL(Right) = VAL(Up) = VAL(Down) = HUGE_REAL;

   fRev = fFalse;
   ctype = backctype = CTYPE_OMIT;
   fDepthChange = fFalse;

   /* ordering may omit clino reading, so set up default here */
   /* this is also used if clino reading is the omit character */
   VAL(Clino) = VAL(BackClino) = 0;

   again:

   /* We clear these flags in the normal course of events, but if there's an
    * error in a reading, we might not, so make sure it has been cleared here.
    */
   pcs->flags &= ~(BIT(FLAGS_ANON_ONE_END) | BIT(FLAGS_IMPLICIT_SPLAY));
   for (ordering = pcs->ordering; ; ordering++) {
      skipblanks();
      switch (*ordering) {
       case Fr:
	  fr = read_prefix(PFX_STATION|PFX_ALLOW_ROOT|PFX_ANON);
	  if (first_stn == End) first_stn = Fr;
	  break;
       case To:
	  to = read_prefix(PFX_STATION|PFX_ALLOW_ROOT|PFX_ANON);
	  if (first_stn == End) first_stn = To;
	  break;
       case Station:
	  fr = to;
	  to = read_prefix(PFX_STATION);
	  first_stn = To;
	  break;
       case Dir: {
	  typedef enum {
	     DIR_NULL=-1, DIR_FORE, DIR_BACK
	  } dir_tok;
	  static const sztok dir_tab[] = {
	     {"B",     DIR_BACK},
	     {"F",     DIR_FORE},
	  };
	  dir_tok tok;
	  get_token();
	  tok = match_tok(dir_tab, TABSIZE(dir_tab));
	  switch (tok) {
	   case DIR_FORE:
	     break;
	   case DIR_BACK:
	     fRev = fTrue;
	     break;
	   default:
	     compile_diagnostic(DIAG_ERR|DIAG_BUF|DIAG_SKIP, /*Found “%s”, expecting “F” or “B”*/131, buffer);
	     process_eol();
	     return;
	  }
	  break;
       }
       case Tape: case BackTape: {
	  reading r = *ordering;
	  read_reading(r, fTrue);
	  if (VAL(r) == HUGE_REAL) {
	     if (!isOmit(ch)) {
		compile_diagnostic_token_show(DIAG_ERR, /*Expecting numeric field, found “%s”*/9);
		/* Avoid also warning about omitted tape reading. */
		VAL(r) = 0;
	     } else {
		nextch();
	     }
	  } else if (VAL(r) < (real)0.0) {
	     compile_diagnostic_reading(DIAG_WARN, r, /*Negative tape reading*/60);
	  }
	  break;
       }
       case Count:
	  VAL(FrCount) = VAL(ToCount);
	  LOC(FrCount) = LOC(ToCount);
	  WID(FrCount) = WID(ToCount);
	  read_reading(ToCount, fFalse);
	  fTopofil = fTrue;
	  break;
       case FrCount:
	  read_reading(FrCount, fFalse);
	  break;
       case ToCount:
	  read_reading(ToCount, fFalse);
	  fTopofil = fTrue;
	  break;
       case Comp: case BackComp:
	  read_bearing_or_omit(*ordering);
	  break;
       case Clino: case BackClino: {
	  reading r = *ordering;
	  clino_type * p_ctype = (r == Clino ? &ctype : &backctype);
	  read_reading(r, fTrue);
	  if (VAL(r) == HUGE_REAL) {
	     VAL(r) = handle_plumb(p_ctype);
	     if (VAL(r) != HUGE_REAL) break;
	     compile_diagnostic_token_show(DIAG_ERR, /*Expecting numeric field, found “%s”*/9);
	     skipline();
	     process_eol();
	     return;
	  }
	  *p_ctype = CTYPE_READING;
	  break;
       }
       case FrDepth: case ToDepth:
	  read_reading(*ordering, fFalse);
	  break;
       case Depth:
	  VAL(FrDepth) = VAL(ToDepth);
	  LOC(FrDepth) = LOC(ToDepth);
	  WID(FrDepth) = WID(ToDepth);
	  read_reading(ToDepth, fFalse);
	  break;
       case DepthChange:
	  fDepthChange = fTrue;
	  VAL(FrDepth) = 0;
	  read_reading(ToDepth, fFalse);
	  break;
       case CompassDATComp:
	  read_bearing_or_omit(Comp);
	  if (is_compass_NaN(VAL(Comp))) VAL(Comp) = HUGE_REAL;
	  break;
       case CompassDATBackComp:
	  read_bearing_or_omit(BackComp);
	  if (is_compass_NaN(VAL(BackComp))) VAL(BackComp) = HUGE_REAL;
	  break;
       case CompassDATClino: case CompassDATBackClino: {
	  reading r;
	  clino_type * p_ctype;
	  if (*ordering == CompassDATClino) {
	     r = Clino;
	     p_ctype = &ctype;
	  } else {
	     r = BackClino;
	     p_ctype = &backctype;
	  }
	  read_reading(r, fFalse);
	  if (is_compass_NaN(VAL(r))) {
	     VAL(r) = HUGE_REAL;
	     *p_ctype = CTYPE_OMIT;
	  } else {
	     *p_ctype = CTYPE_READING;
	  }
	  break;
       }
       case CompassDATLeft: case CompassDATRight:
       case CompassDATUp: case CompassDATDown: {
	  /* FIXME: need to actually make use of these entries! */
	  reading actual = Left + (*ordering - CompassDATLeft);
	  read_reading(actual, fFalse);
	  if (VAL(actual) < 0) VAL(actual) = HUGE_REAL;
	  break;
       }
       case CompassDATFlags:
	  if (ch == '#') {
	     filepos fp;
	     get_pos(&fp);
	     nextch();
	     if (ch == '|') {
		nextch();
		while (ch >= 'A' && ch <= 'Z') {
		   compass_dat_flags |= BIT(ch - 'A');
		   /* We currently understand:
		    *   L (exclude from length)
		    *   X (exclude data)
		    * FIXME: but should also handle at least some of:
		    *   C (no adjustment) (set all (co)variances to 0?)
		    *   P (no plot) (new flag in 3d for "hidden by default"?)
		    */
		   nextch();
		}
		if (ch == '#') {
		   nextch();
		} else {
		   compass_dat_flags = 0;
		   set_pos(&fp);
		}
	     } else {
		set_pos(&fp);
	     }
	  }
	  break;
       case Ignore:
	  skipword(); break;
       case IgnoreAllAndNewLine:
	  skipline();
	  /* fall through */
       case Newline:
	  if (fr != NULL) {
	     int r;
	     int save_flags;
	     int implicit_splay;
	     if (fTopofil) {
		VAL(Tape) = VAL(ToCount) - VAL(FrCount);
		LOC(Tape) = LOC(ToCount);
		WID(Tape) = WID(ToCount);
	     }
	     /* Note: frdepth == todepth test works regardless of fDepthChange
	      * (frdepth always zero, todepth is change of depth) and also
	      * works for STYLE_NORMAL (both remain 0) */
	     if (TSTBIT(pcs->infer, INFER_EQUATES) &&
		 (VAL(Tape) == (real)0.0 || VAL(Tape) == HUGE_REAL) &&
		 (VAL(BackTape) == (real)0.0 || VAL(BackTape) == HUGE_REAL) &&
		 VAL(FrDepth) == VAL(ToDepth)) {
		process_equate(fr, to);
		goto inferred_equate;
	     }
	     if (fRev) {
		prefix *t = fr;
		fr = to;
		to = t;
	     }
	     if (fTopofil) {
		VAL(Tape) *= pcs->units[Q_COUNT] * pcs->sc[Q_COUNT];
	     } else if (VAL(Tape) != HUGE_REAL) {
		VAL(Tape) *= pcs->units[Q_LENGTH];
		VAL(Tape) -= pcs->z[Q_LENGTH];
		VAL(Tape) *= pcs->sc[Q_LENGTH];
	     }
	     if (VAL(BackTape) != HUGE_REAL) {
		VAL(BackTape) *= pcs->units[Q_BACKLENGTH];
		VAL(BackTape) -= pcs->z[Q_BACKLENGTH];
		VAL(BackTape) *= pcs->sc[Q_BACKLENGTH];
		if (VAL(Tape) != HUGE_REAL) {
		   real diff = VAL(Tape) - VAL(BackTape);
		   if (sqrd(diff / 3.0) > VAR(Tape) + VAR(BackTape)) {
		      /* fore and back readings differ by more than 3 sds */
		      /* TRANSLATORS: %s is replaced by the amount the readings disagree
		       * by, e.g. "0.12m" or "0.2ft". */
		      warn_readings_differ(/*TAPE reading and BACKTAPE reading disagree by %s*/97,
					   diff, get_length_units(Q_LENGTH));
		   }
		   VAL(Tape) = VAL(Tape) / VAR(Tape) + VAL(BackTape) / VAR(BackTape);
		   VAR(Tape) = (VAR(Tape) + VAR(BackTape)) / 4;
		   VAL(Tape) *= VAR(Tape);
		} else {
		   VAL(Tape) = VAL(BackTape);
		   VAR(Tape) = VAR(BackTape);
		}
	     } else if (VAL(Tape) == HUGE_REAL) {
		compile_diagnostic_reading(DIAG_ERR, Tape, /*Tape reading may not be omitted*/94);
		goto inferred_equate;
	     }
	     implicit_splay = TSTBIT(pcs->flags, FLAGS_IMPLICIT_SPLAY);
	     pcs->flags &= ~(BIT(FLAGS_ANON_ONE_END) | BIT(FLAGS_IMPLICIT_SPLAY));
	     save_flags = pcs->flags;
	     if (implicit_splay) {
		pcs->flags |= BIT(FLAGS_SPLAY);
	     }
	     switch (pcs->style) {
	      case STYLE_NORMAL:
		r = process_normal(fr, to, (first_stn == To) ^ fRev,
				   ctype, backctype);
		break;
	      case STYLE_DIVING:
		/* FIXME: Handle any clino readings */
		r = process_diving(fr, to, (first_stn == To) ^ fRev,
				   fDepthChange);
		break;
	      case STYLE_CYLPOLAR:
		r = process_cylpolar(fr, to, (first_stn == To) ^ fRev,
				     fDepthChange);
		break;
	      default:
		r = 0; /* avoid warning */
		BUG("bad style");
	     }
	     pcs->flags = save_flags;
	     if (!r) skipline();

	     /* Swap fr and to back to how they were for next line */
	     if (fRev) {
		prefix *t = fr;
		fr = to;
		to = t;
	     }
	  }

	  fRev = fFalse;
	  ctype = backctype = CTYPE_OMIT;
	  fDepthChange = fFalse;

	  /* ordering may omit clino reading, so set up default here */
	  /* this is also used if clino reading is the omit character */
	  VAL(Clino) = VAL(BackClino) = 0;
	  LOC(Clino) = LOC(BackClino) = -1;
	  WID(Clino) = WID(BackClino) = 0;

	  inferred_equate:

	  fMulti = fTrue;
	  while (1) {
	      process_eol();
	      skipblanks();
	      if (isData(ch)) break;
	      if (!isComm(ch)) {
		 return;
	      }
	  }
	  break;
       case IgnoreAll:
	  skipline();
	  /* fall through */
       case End:
	  if (!fMulti) {
	     int save_flags;
	     int implicit_splay;
	     /* Compass ignore flag is 'X' */
	     if ((compass_dat_flags & BIT('X' - 'A'))) {
		process_eol();
		return;
	     }
	     if (fRev) {
		prefix *t = fr;
		fr = to;
		to = t;
	     }
	     if (fTopofil) {
		VAL(Tape) = VAL(ToCount) - VAL(FrCount);
		LOC(Tape) = LOC(ToCount);
		WID(Tape) = WID(ToCount);
	     }
	     /* Note: frdepth == todepth test works regardless of fDepthChange
	      * (frdepth always zero, todepth is change of depth) and also
	      * works for STYLE_NORMAL (both remain 0) */
	     if (TSTBIT(pcs->infer, INFER_EQUATES) &&
		 (VAL(Tape) == (real)0.0 || VAL(Tape) == HUGE_REAL) &&
		 (VAL(BackTape) == (real)0.0 || VAL(BackTape) == HUGE_REAL) &&
		 VAL(FrDepth) == VAL(ToDepth)) {
		process_equate(fr, to);
		process_eol();
		return;
	     }
	     if (fTopofil) {
		VAL(Tape) *= pcs->units[Q_COUNT] * pcs->sc[Q_COUNT];
	     } else if (VAL(Tape) != HUGE_REAL) {
		VAL(Tape) *= pcs->units[Q_LENGTH];
		VAL(Tape) -= pcs->z[Q_LENGTH];
		VAL(Tape) *= pcs->sc[Q_LENGTH];
	     }
	     if (VAL(BackTape) != HUGE_REAL) {
		VAL(BackTape) *= pcs->units[Q_BACKLENGTH];
		VAL(BackTape) -= pcs->z[Q_BACKLENGTH];
		VAL(BackTape) *= pcs->sc[Q_BACKLENGTH];
		if (VAL(Tape) != HUGE_REAL) {
		   real diff = VAL(Tape) - VAL(BackTape);
		   if (sqrd(diff / 3.0) > VAR(Tape) + VAR(BackTape)) {
		      /* fore and back readings differ by more than 3 sds */
		      /* TRANSLATORS: %s is replaced by the amount the readings disagree
		       * by, e.g. "0.12m" or "0.2ft". */
		      warn_readings_differ(/*TAPE reading and BACKTAPE reading disagree by %s*/97,
					   diff, get_length_units(Q_LENGTH));
		   }
		   VAL(Tape) = VAL(Tape) / VAR(Tape) + VAL(BackTape) / VAR(BackTape);
		   VAR(Tape) = (VAR(Tape) + VAR(BackTape)) / 4;
		   VAL(Tape) *= VAR(Tape);
		} else {
		   VAL(Tape) = VAL(BackTape);
		   VAR(Tape) = VAR(BackTape);
		}
	     } else if (VAL(Tape) == HUGE_REAL) {
		compile_diagnostic_reading(DIAG_ERR, Tape, /*Tape reading may not be omitted*/94);
		process_eol();
		return;
	     }
	     implicit_splay = TSTBIT(pcs->flags, FLAGS_IMPLICIT_SPLAY);
	     pcs->flags &= ~(BIT(FLAGS_ANON_ONE_END) | BIT(FLAGS_IMPLICIT_SPLAY));
	     save_flags = pcs->flags;
	     if (implicit_splay) {
		pcs->flags |= BIT(FLAGS_SPLAY);
	     }
	     if ((compass_dat_flags & BIT('L' - 'A'))) {
		/* 'L' means "exclude from length" - map this to Survex's
		 * FLAGS_DUPLICATE. */
		pcs->flags |= BIT(FLAGS_DUPLICATE);
	     }
	     switch (pcs->style) {
	      case STYLE_NORMAL:
		process_normal(fr, to, (first_stn == To) ^ fRev,
			       ctype, backctype);
		break;
	      case STYLE_DIVING:
		/* FIXME: Handle any clino readings */
		process_diving(fr, to, (first_stn == To) ^ fRev,
			       fDepthChange);
		break;
	      case STYLE_CYLPOLAR:
		process_cylpolar(fr, to, (first_stn == To) ^ fRev,
				 fDepthChange);
		break;
	      default:
		BUG("bad style");
	     }
	     pcs->flags = save_flags;

	     process_eol();
	     return;
	  }
	  do {
	     process_eol();
	     skipblanks();
	  } while (isComm(ch));
	  goto again;
       default:
	  BUG("Unknown reading in ordering");
      }
   }
}

static int
process_lrud(prefix *stn)
{
   SVX_ASSERT(next_lrud);
   lrud * xsect = osnew(lrud);
   xsect->stn = stn;
   xsect->l = (VAL(Left) * pcs->units[Q_LEFT] - pcs->z[Q_LEFT]) * pcs->sc[Q_LEFT];
   xsect->r = (VAL(Right) * pcs->units[Q_RIGHT] - pcs->z[Q_RIGHT]) * pcs->sc[Q_RIGHT];
   xsect->u = (VAL(Up) * pcs->units[Q_UP] - pcs->z[Q_UP]) * pcs->sc[Q_UP];
   xsect->d = (VAL(Down) * pcs->units[Q_DOWN] - pcs->z[Q_DOWN]) * pcs->sc[Q_DOWN];
   xsect->meta = pcs->meta;
   if (pcs->meta) ++pcs->meta->ref_count;
   xsect->next = NULL;
   *next_lrud = xsect;
   next_lrud = &(xsect->next);

   return 1;
}

static void
data_passage(void)
{
   prefix *stn = NULL;
   const reading *ordering;

   for (ordering = pcs->ordering ; ; ordering++) {
      skipblanks();
      switch (*ordering) {
       case Station:
	 stn = read_prefix(PFX_STATION);
	 break;
       case Left: case Right: case Up: case Down: {
	 reading r = *ordering;
	 read_reading(r, fTrue);
	 if (VAL(r) == HUGE_REAL) {
	    if (!isOmit(ch)) {
	       compile_diagnostic_token_show(DIAG_ERR, /*Expecting numeric field, found “%s”*/9);
	    } else {
	       nextch();
	    }
	    VAL(r) = -1;
	 }
	 break;
       }
       case Ignore:
	 skipword(); break;
       case IgnoreAll:
	 skipline();
	 /* fall through */
       case End: {
	 process_lrud(stn);
	 process_eol();
	 return;
       }
       default: BUG("Unknown reading in ordering");
      }
   }
}

static int
process_nosurvey(prefix *fr, prefix *to, bool fToFirst)
{
   nosurveylink *link;

   /* Suppress "unused fixed point" warnings for these stations */
   fr->sflags |= BIT(SFLAGS_USED);
   to->sflags |= BIT(SFLAGS_USED);

   /* add to linked list which is dealt with after network is solved */
   link = osnew(nosurveylink);
   if (fToFirst) {
      link->to = StnFromPfx(to);
      link->fr = StnFromPfx(fr);
   } else {
      link->fr = StnFromPfx(fr);
      link->to = StnFromPfx(to);
   }
   link->flags = pcs->flags | (STYLE_NOSURVEY << FLAGS_STYLE_BIT0);
   link->meta = pcs->meta;
   if (pcs->meta) ++pcs->meta->ref_count;
   link->next = nosurveyhead;
   nosurveyhead = link;
   return 1;
}

static void
data_nosurvey(void)
{
   prefix *fr = NULL, *to = NULL;

   bool fMulti = fFalse;

   reading first_stn = End;

   const reading *ordering;

   again:

   for (ordering = pcs->ordering ; ; ordering++) {
      skipblanks();
      switch (*ordering) {
       case Fr:
	  fr = read_prefix(PFX_STATION|PFX_ALLOW_ROOT);
	  if (first_stn == End) first_stn = Fr;
	  break;
       case To:
	  to = read_prefix(PFX_STATION|PFX_ALLOW_ROOT);
	  if (first_stn == End) first_stn = To;
	  break;
       case Station:
	  fr = to;
	  to = read_prefix(PFX_STATION);
	  first_stn = To;
	  break;
       case Ignore:
	 skipword(); break;
       case IgnoreAllAndNewLine:
	 skipline();
	 /* fall through */
       case Newline:
	 if (fr != NULL) {
	    if (!process_nosurvey(fr, to, first_stn == To))
	       skipline();
	 }
	 if (ordering[1] == End) {
	    do {
	       process_eol();
	       skipblanks();
	    } while (isComm(ch));
	    if (!isData(ch)) {
	       return;
	    }
	    goto again;
	 }
	 fMulti = fTrue;
	 while (1) {
	    process_eol();
	    skipblanks();
	    if (isData(ch)) break;
	    if (!isComm(ch)) {
	       return;
	    }
	 }
	 break;
       case IgnoreAll:
	 skipline();
	 /* fall through */
       case End:
	 if (!fMulti) {
	    (void)process_nosurvey(fr, to, first_stn == To);
	    process_eol();
	    return;
	 }
	 do {
	    process_eol();
	    skipblanks();
	 } while (isComm(ch));
	 goto again;
       default: BUG("Unknown reading in ordering");
      }
   }
}

/* totally ignore a line of survey data */
static void
data_ignore(void)
{
   skipline();
   process_eol();
}
