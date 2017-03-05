/*
 * field.c - routines for dealing with fields and record parsing
 */

/*
 * Copyright (C) 1986, 1988, 1989, 1991-2016 the Free Software Foundation, Inc.
 *
 * This file is part of GAWK, the GNU implementation of the
 * AWK Programming Language.
 *
 * GAWK is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * GAWK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "awk.h"

/*
 * In case that the system doesn't have isblank().
 * Don't bother with autoconf ifdef junk, just force it.
 * See dfa.c and regex_internal.h and regcomp.c. Bleah.
 */
static int
is_blank(int c)
{
	return c == ' ' || c == '\t';
}

typedef void (* Setfunc)(long, char *, long, NODE *);

static long (*parse_field)(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static long (*save_parse_field)(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static long re_parse_field(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static long def_parse_field(long, char **, int, NODE *,
			      Regexp *, Setfunc, NODE *, NODE *, bool);
static long null_parse_field(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static long sc_parse_field(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static long fw_parse_field(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static long api_parse_field(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static const int *api_fw = NULL;
static long fpat_parse_field(long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *, NODE *, bool);
static void set_element(long num, char * str, long len, NODE *arr);
static void grow_fields_arr(long num);
static void set_field(long num, char *str, long len, NODE *dummy);
static void purge_record(void);

static char *parse_extent;	/* marks where to restart parse of record */
static long parse_high_water = 0; /* field number that we have parsed so far */
static long nf_high_water = 0;	/* size of fields_arr */
static bool resave_fs;
static NODE *save_FS;		/* save current value of FS when line is read,
				 * to be used in deferred parsing
				 */
static int *FIELDWIDTHS = NULL;

NODE **fields_arr;		/* array of pointers to the field nodes */
bool field0_valid;		/* $(>0) has not been changed yet */
int default_FS;			/* true when FS == " " */
Regexp *FS_re_yes_case = NULL;
Regexp *FS_re_no_case = NULL;
Regexp *FS_regexp = NULL;
Regexp *FPAT_re_yes_case = NULL;
Regexp *FPAT_re_no_case = NULL;
Regexp *FPAT_regexp = NULL;
NODE *Null_field = NULL;

/* init_fields --- set up the fields array to start with */

void
init_fields()
{
	emalloc(fields_arr, NODE **, sizeof(NODE *), "init_fields");

	getnode(fields_arr[0]);
	*fields_arr[0] = *Nnull_string;
	fields_arr[0]->flags |= NULL_FIELD;

	parse_extent = fields_arr[0]->stptr;
	save_FS = dupnode(FS_node->var_value);

	getnode(Null_field);
	*Null_field = *Nnull_string;
	Null_field->valref = 1;
	Null_field->flags = (STRCUR|STRING|NULL_FIELD); /* do not set MALLOC */

	field0_valid = true;
}

/* grow_fields --- acquire new fields as needed */

static void
grow_fields_arr(long num)
{
	int t;
	NODE *n;

	erealloc(fields_arr, NODE **, (num + 1) * sizeof(NODE *), "grow_fields_arr");
	for (t = nf_high_water + 1; t <= num; t++) {
		getnode(n);
		*n = *Null_field;
		fields_arr[t] = n;
	}
	nf_high_water = num;
}

/* set_field --- set the value of a particular field */

/*ARGSUSED*/
static void
set_field(long num,
	char *str,
	long len,
	NODE *dummy ATTRIBUTE_UNUSED)	/* just to make interface same as set_element */
{
	NODE *n;

	if (num > nf_high_water)
		grow_fields_arr(num);
	n = fields_arr[num];
	n->stptr = str;
	n->stlen = len;
	n->flags = (STRCUR|STRING|USER_INPUT);	/* do not set MALLOC */
}

/* rebuild_record --- Someone assigned a value to $(something).
			Fix up $0 to be right */

void
rebuild_record()
{
	/*
	 * use explicit unsigned longs for lengths, in case
	 * a size_t isn't big enough.
	 */
	unsigned long tlen;
	NODE *tmp;
	char *ops;
	char *cops;
	long i;

	assert(NF != -1);

	tlen = 0;
	for (i = NF; i > 0; i--) {
		tmp = fields_arr[i];
		tmp = force_string(tmp);
		tlen += tmp->stlen;
	}
	tlen += (NF - 1) * OFSlen;
	if ((long) tlen < 0)
		tlen = 0;
	emalloc(ops, char *, tlen + 1, "rebuild_record");
	cops = ops;
	ops[0] = '\0';
	for (i = 1;  i <= NF; i++) {
		free_wstr(fields_arr[i]);
		tmp = fields_arr[i];
		/* copy field */
		if (tmp->stlen == 1)
			*cops++ = tmp->stptr[0];
		else if (tmp->stlen != 0) {
			memcpy(cops, tmp->stptr, tmp->stlen);
			cops += tmp->stlen;
		}
		/* copy OFS */
		if (i != NF) {
			if (OFSlen == 1)
				*cops++ = *OFS;
			else if (OFSlen != 0) {
				memcpy(cops, OFS, OFSlen);
				cops += OFSlen;
			}
		}
	}
	tmp = make_str_node(ops, tlen, ALREADY_MALLOCED);

	/*
	 * Since we are about to unref fields_arr[0], we want to find
	 * any fields that still point into it, and have them point
	 * into the new field zero.  This has to be done intelligently,
	 * so that unrefing a field doesn't try to unref into the old $0.
	 */
	for (cops = ops, i = 1; i <= NF; i++) {
		NODE *r = fields_arr[i];
		/*
		 * There is no reason to copy malloc'ed fields to point into
		 * the new $0 buffer, although that's how previous versions did
		 * it. It seems faster to leave the malloc'ed fields in place.
		 */
		if (r->stlen > 0 && (r->flags & MALLOC) == 0) {
			NODE *n;
			getnode(n);

			*n = *r;
			if (r->valref > 1) {
				/*
				 * This probably never happens, since it
				 * was not considered by previous versions of
				 * this function. But it seems clear that
				 * we can't leave r's stptr pointing into the
				 * old $0 buffer that we are about to unref.
				 * It's not a priori obvious that valref must be
				 * 1 in all cases, so it seems wise to suppport
				 * this corner case. The only question is
				 * whether to add a warning message.
				 */
				emalloc(r->stptr, char *, r->stlen + 1, "rebuild_record");
				memcpy(r->stptr, cops, r->stlen);
				r->stptr[r->stlen] = '\0';
				r->flags |= MALLOC;
			}

			n->stptr = cops;
			unref(r);
			fields_arr[i] = n;
			assert((n->flags & WSTRCUR) == 0);
		}
		cops += fields_arr[i]->stlen + OFSlen;
	}

	assert((fields_arr[0]->flags & MALLOC) == 0
		? fields_arr[0]->valref == 1
		: true);

	unref(fields_arr[0]);

	fields_arr[0] = tmp;
	field0_valid = true;
}

/*
 * set_record:
 * setup $0, but defer parsing rest of line until reference is made to $(>0)
 * or to NF.  At that point, parse only as much as necessary.
 *
 * Manage a private buffer for the contents of $0.  Doing so keeps us safe
 * if `getline var' decides to rearrange the contents of the IOBUF that
 * $0 might have been pointing into.  The cost is the copying of the buffer;
 * but better correct than fast.
 */
void
set_record(const char *buf, int cnt, const int *fw)
{
	NODE *n;
	static char *databuf;
	static unsigned long databuf_size;
#define INITIAL_SIZE	512
#define MAX_SIZE	((unsigned long) ~0)	/* maximally portable ... */

	purge_record();

	/* buffer management: */
	if (databuf_size == 0) {	/* first time */
		emalloc(databuf, char *, INITIAL_SIZE, "set_record");
		databuf_size = INITIAL_SIZE;
		memset(databuf, '\0', INITIAL_SIZE);

	}
	/*
	 * Make sure there's enough room. Since we sometimes need
	 * to place a sentinel at the end, we make sure
	 * databuf_size is > cnt after allocation.
	 */
	if (cnt >= databuf_size) {
		do {
			if (databuf_size > MAX_SIZE/2)
				fatal(_("input record too large"));
			databuf_size *= 2;
		} while (cnt >= databuf_size);
		erealloc(databuf, char *, databuf_size, "set_record");
		memset(databuf, '\0', databuf_size);
	}
	/* copy the data */
	memcpy(databuf, buf, cnt);

	/*
	 * Add terminating '\0' so that C library routines
	 * will know when to stop.
	 */
	databuf[cnt] = '\0';

	/* manage field 0: */
	assert((fields_arr[0]->flags & MALLOC) == 0
		? fields_arr[0]->valref == 1
		: true);

	unref(fields_arr[0]);
	getnode(n);
	n->stptr = databuf;
	n->stlen = cnt;
	n->valref = 1;
	n->type = Node_val;
	n->stfmt = STFMT_UNUSED;
	n->flags = (STRING|STRCUR|USER_INPUT);	/* do not set MALLOC */
	fields_arr[0] = n;
	if (fw != api_fw) {
		if ((api_fw = fw) != NULL) {
			if (parse_field != api_parse_field) {
				parse_field = api_parse_field;
				update_PROCINFO_str("FS", "API");
			}
		}
		else {
			if (parse_field != save_parse_field) {
				parse_field = save_parse_field;
				update_PROCINFO_str("FS", current_field_sep_str());
			}
		}
	}

#undef INITIAL_SIZE
#undef MAX_SIZE
}

/* reset_record --- start over again with current $0 */

void
reset_record()
{
	fields_arr[0] = force_string(fields_arr[0]);
	purge_record();
}

static void
purge_record()
{
	int i;
	NODE *n;

	NF = -1;
	for (i = 1; i <= parse_high_water; i++) {
		assert((fields_arr[i]->flags & MALLOC) == 0
			? fields_arr[i]->valref == 1
			: true);
		unref(fields_arr[i]);
		getnode(n);
		*n = *Null_field;
		fields_arr[i] = n;
	}

	parse_high_water = 0;
	/*
	 * $0 = $0 should resplit using the current value of FS.
	 */
	if (resave_fs) {
		resave_fs = false;
		unref(save_FS);
		save_FS = dupnode(FS_node->var_value);
	}

	field0_valid = true;
}

/* set_NF --- handle what happens to $0 and fields when NF is changed */

void
set_NF()
{
	int i;
	long nf;
	NODE *n;

	assert(NF != -1);

	(void) force_number(NF_node->var_value);
	nf = get_number_si(NF_node->var_value);
	if (nf < 0)
		fatal(_("NF set to negative value"));
	NF = nf;

	if (NF > nf_high_water)
		grow_fields_arr(NF);
	if (parse_high_water < NF) {
		for (i = parse_high_water + 1; i >= 0 && i <= NF; i++) {
			unref(fields_arr[i]);
			getnode(n);
			*n = *Null_field;
			fields_arr[i] = n;
		}
		parse_high_water = NF;
	} else if (parse_high_water > 0) {
		for (i = NF + 1; i >= 0 && i <= parse_high_water; i++) {
			unref(fields_arr[i]);
			getnode(n);
			*n = *Null_field;
			fields_arr[i] = n;
		}
		parse_high_water = NF;
	}
	field0_valid = false;
}

/*
 * re_parse_field --- parse fields using a regexp.
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a regular
 * expression -- either user-defined or because RS=="" and FS==" "
 */
static long
re_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *sep_arr,  /* array of field separators (maybe NULL) */
	bool in_middle)
{
	char *scan = *buf;
	long nf = parse_high_water;
	char *field;
	char *end = scan + len;
	int regex_flags = RE_NEED_START;
	char *sep;
	size_t mbclen = 0;
	mbstate_t mbs;

	memset(&mbs, 0, sizeof(mbstate_t));

	if (in_middle)
		regex_flags |= RE_NO_BOL;

	if (up_to == UNLIMITED)
		nf = 0;
	if (len == 0)
		return nf;

	if (RS_is_null && default_FS) {
		sep = scan;
		while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\n'))
			scan++;
		if (sep_arr != NULL && sep < scan)
			set_element(nf, sep, (long)(scan - sep), sep_arr);
	}

	if (rp == NULL) /* use FS */
		rp = FS_regexp;

	field = scan;
	while (scan < end
	       && research(rp, scan, 0, (end - scan), regex_flags) != -1
	       && nf < up_to) {
		regex_flags |= RE_NO_BOL;
		if (REEND(rp, scan) == RESTART(rp, scan)) {   /* null match */
			if (gawk_mb_cur_max > 1)	{
				mbclen = mbrlen(scan, end-scan, &mbs);
				if ((mbclen == 1) || (mbclen == (size_t) -1)
					|| (mbclen == (size_t) -2) || (mbclen == 0)) {
					/* We treat it as a singlebyte character.  */
					mbclen = 1;
				}
				scan += mbclen;
			} else
				scan++;
			if (scan == end) {
				(*set)(++nf, field, (long)(scan - field), n);
				up_to = nf;
				break;
			}
			continue;
		}
		(*set)(++nf, field,
		       (long)(scan + RESTART(rp, scan) - field), n);
		if (sep_arr != NULL)
	    		set_element(nf, scan + RESTART(rp, scan),
           			(long) (REEND(rp, scan) - RESTART(rp, scan)), sep_arr);
		scan += REEND(rp, scan);
		field = scan;
		if (scan == end)	/* FS at end of record */
			(*set)(++nf, field, 0L, n);
	}
	if (nf != up_to && scan < end) {
		(*set)(++nf, scan, (long)(end - scan), n);
		scan = end;
	}
	*buf = scan;
	return nf;
}

/*
 * def_parse_field --- default field parsing.
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a single space
 * character.
 */

static long
def_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *sep_arr,  /* array of field separators (maybe NULL) */
	bool in_middle ATTRIBUTE_UNUSED)
{
	char *scan = *buf;
	long nf = parse_high_water;
	char *field;
	char *end = scan + len;
	char sav;
	char *sep;

	if (up_to == UNLIMITED)
		nf = 0;
	if (len == 0)
		return nf;

	/*
	 * Nasty special case. If FS set to "", return whole record
	 * as first field. This is not worth a separate function.
	 */
	if (fs->stlen == 0) {
		(*set)(++nf, *buf, len, n);
		*buf += len;
		return nf;
	}

	/* before doing anything save the char at *end */
	sav = *end;
	/* because it will be destroyed now: */

	*end = ' ';	/* sentinel character */
	sep = scan;
	for (; nf < up_to; scan++) {
		/*
		 * special case:  fs is single space, strip leading whitespace
		 */
		while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\n'))
			scan++;

		if (sep_arr != NULL && scan > sep)
			set_element(nf, sep, (long) (scan - sep), sep_arr);

		if (scan >= end)
			break;

		field = scan;

		while (*scan != ' ' && *scan != '\t' && *scan != '\n')
			scan++;

		(*set)(++nf, field, (long)(scan - field), n);

		if (scan == end)
			break;

		sep = scan;
	}

	/* everything done, restore original char at *end */
	*end = sav;

	*buf = scan;
	return nf;
}

/*
 * null_parse_field --- each character is a separate field
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is the null string.
 */
static long
null_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *sep_arr,  /* array of field separators (maybe NULL) */
	bool in_middle ATTRIBUTE_UNUSED)
{
	char *scan = *buf;
	long nf = parse_high_water;
	char *end = scan + len;

	if (up_to == UNLIMITED)
		nf = 0;
	if (len == 0)
		return nf;

	if (gawk_mb_cur_max > 1) {
		mbstate_t mbs;
		memset(&mbs, 0, sizeof(mbstate_t));
		for (; nf < up_to && scan < end;) {
			size_t mbclen = mbrlen(scan, end-scan, &mbs);
			if ((mbclen == 1) || (mbclen == (size_t) -1)
				|| (mbclen == (size_t) -2) || (mbclen == 0)) {
				/* We treat it as a singlebyte character.  */
				mbclen = 1;
			}
			if (sep_arr != NULL && nf > 0)
				set_element(nf, scan, 0L, sep_arr);
			(*set)(++nf, scan, mbclen, n);
			scan += mbclen;
		}
	} else {
		for (; nf < up_to && scan < end; scan++) {
			if (sep_arr != NULL && nf > 0)
				set_element(nf, scan, 0L, sep_arr);
			(*set)(++nf, scan, 1L, n);
		}
	}

	*buf = scan;
	return nf;
}

/*
 * sc_parse_field --- single character field separator
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a single character
 * other than space.
 */
static long
sc_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *sep_arr,  /* array of field separators (maybe NULL) */
	bool in_middle ATTRIBUTE_UNUSED)
{
	char *scan = *buf;
	char fschar;
	long nf = parse_high_water;
	char *field;
	char *end = scan + len;
	char sav;
	size_t mbclen = 0;
	mbstate_t mbs;

	memset(&mbs, 0, sizeof(mbstate_t));

	if (up_to == UNLIMITED)
		nf = 0;
	if (len == 0)
		return nf;

	if (RS_is_null && fs->stlen == 0)
		fschar = '\n';
	else
		fschar = fs->stptr[0];

	/* before doing anything save the char at *end */
	sav = *end;
	/* because it will be destroyed now: */
	*end = fschar;	/* sentinel character */

	for (; nf < up_to;) {
		field = scan;
		if (gawk_mb_cur_max > 1) {
			while (*scan != fschar) {
				mbclen = mbrlen(scan, end-scan, &mbs);
				if ((mbclen == 1) || (mbclen == (size_t) -1)
					|| (mbclen == (size_t) -2) || (mbclen == 0)) {
					/* We treat it as a singlebyte character.  */
					mbclen = 1;
				}
				scan += mbclen;
			}
		} else {
			while (*scan != fschar)
				scan++;
		}
		(*set)(++nf, field, (long)(scan - field), n);
		if (scan == end)
			break;
		if (sep_arr != NULL)
			set_element(nf, scan, 1L, sep_arr);
		scan++;
		if (scan == end) {	/* FS at end of record */
			(*set)(++nf, field, 0L, n);
			break;
		}
	}

	/* everything done, restore original char at *end */
	*end = sav;

	*buf = scan;
	return nf;
}

/*
 * fw_parse_field --- field parsing using FIELDWIDTHS spec
 *
 * This is called from get_field() via (*parse_field)().
 * This variation is for fields are fixed widths.
 */
static long
fw_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *dummy ATTRIBUTE_UNUSED, /* sep_arr not needed here: hence dummy */
	bool in_middle ATTRIBUTE_UNUSED)
{
	char *scan = *buf;
	long nf = parse_high_water;
	char *end = scan + len;
	int nmbc;
	size_t mbclen;
	size_t mbslen;
	size_t lenrest;
	char *mbscan;
	mbstate_t mbs;

	memset(&mbs, 0, sizeof(mbstate_t));

	if (up_to == UNLIMITED)
		nf = 0;
	if (len == 0)
		return nf;
	for (; nf < up_to && (len = FIELDWIDTHS[nf+1]) != -1; ) {
		if (gawk_mb_cur_max > 1) {
			nmbc = 0;
			mbslen = 0;
			mbscan = scan;
			lenrest = end - scan;
			while (nmbc < len && mbslen < lenrest) {
				mbclen = mbrlen(mbscan, end - mbscan, &mbs);
				if (   mbclen == 1
				    || mbclen == (size_t) -1
				    || mbclen == (size_t) -2
				    || mbclen == 0) {
					/* We treat it as a singlebyte character.  */
		    			mbclen = 1;
				}
				if (mbclen <= end - mbscan) {
					mbscan += mbclen;
		    			mbslen += mbclen;
		    			++nmbc;
				}
	    		}
			(*set)(++nf, scan, (long) mbslen, n);
			scan += mbslen;
		} else {
			if (len > end - scan)
				len = end - scan;
			(*set)(++nf, scan, (long) len, n);
			scan += len;
		}
	}
	if (len == -1)
		*buf = end;
	else
		*buf = scan;
	return nf;
}

/*
 * api_parse_field --- field parsing using field widths returned by API parser.
 *
 * This is called from get_field() via (*parse_field)().
 */
static long
api_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *dummy ATTRIBUTE_UNUSED, /* sep_arr not needed here: hence dummy */
	bool in_middle ATTRIBUTE_UNUSED)
{
	char *scan = *buf;
	long nf = parse_high_water;
	char *end = scan + len;
	int skiplen;

	if (up_to == UNLIMITED)
		nf = 0;
	if (len == 0)
		return nf;
	while (nf < up_to) {
		if (((skiplen = api_fw[2*nf]) < 0) ||
		    ((len = api_fw[2*nf+1]) < 0)) {
		    	*buf = end;
			return nf;
		}
		if (skiplen > end - scan)
			skiplen = end - scan;
		scan += skiplen;
		if (len > end - scan)
			len = end - scan;
		(*set)(++nf, scan, (long) len, n);
		scan += len;
	}
	*buf = scan;
	return nf;
}

/* invalidate_field0 --- $0 needs reconstruction */

void
invalidate_field0()
{
	field0_valid = false;
}

/* get_field --- return a particular $n */

/* assign is not NULL if this field is on the LHS of an assign */

NODE **
get_field(long requested, Func_ptr *assign)
{
	bool in_middle = false;
	/*
	 * if requesting whole line but some other field has been altered,
	 * then the whole line must be rebuilt
	 */
	if (requested == 0) {
		if (! field0_valid) {
			/* first, parse remainder of input record */
			if (NF == -1) {
				NF = (*parse_field)(UNLIMITED - 1, &parse_extent,
		    			fields_arr[0]->stlen -
					(parse_extent - fields_arr[0]->stptr),
		    			save_FS, FS_regexp, set_field,
					(NODE *) NULL,
					(NODE *) NULL,
					in_middle);
				parse_high_water = NF;
			}
			rebuild_record();
		}
		if (assign != NULL)
			*assign = reset_record;
		return &fields_arr[0];
	}

	/* assert(requested > 0); */

#if 0
	if (assign != NULL)
		field0_valid = false;		/* $0 needs reconstruction */
#else
	/*
	 * Keep things uniform. Also, mere intention of assigning something
	 * to $n should not make $0 invalid. Makes sense to invalidate $0
	 * after the actual assignment is performed. Not a real issue in
	 * the interpreter otherwise, but causes problem in the
	 * debugger when watching or printing fields.
	 */

	if (assign != NULL)
		*assign = invalidate_field0;	/* $0 needs reconstruction */
#endif

	if (requested <= parse_high_water)	/* already parsed this field */
		return &fields_arr[requested];

	if (NF == -1) {	/* have not yet parsed to end of record */
		/*
		 * parse up to requested fields, calling set_field() for each,
		 * saving in parse_extent the point where the parse left off
		 */
		if (parse_high_water == 0)	/* starting at the beginning */
			parse_extent = fields_arr[0]->stptr;
		else
			in_middle = true;
		parse_high_water = (*parse_field)(requested, &parse_extent,
		     fields_arr[0]->stlen - (parse_extent - fields_arr[0]->stptr),
		     save_FS, NULL, set_field, (NODE *) NULL, (NODE *) NULL, in_middle);

		/*
		 * if we reached the end of the record, set NF to the number of
		 * fields so far.  Note that requested might actually refer to
		 * a field that is beyond the end of the record, but we won't
		 * set NF to that value at this point, since this is only a
		 * reference to the field and NF only gets set if the field
		 * is assigned to -- this case is handled below
		 */
		if (parse_extent == fields_arr[0]->stptr + fields_arr[0]->stlen)
			NF = parse_high_water;
		else if (parse_field == fpat_parse_field) {
			/* FPAT parsing is weird, isolate the special cases */
			char *rec_start = fields_arr[0]->stptr;
			char *rec_end = fields_arr[0]->stptr + fields_arr[0]->stlen;

			if (    parse_extent > rec_end
			    || (parse_extent > rec_start && parse_extent < rec_end && requested == UNLIMITED-1))
				NF = parse_high_water;
			else if (parse_extent == rec_start) /* could be no match for FPAT */
				NF = 0;
		}
		if (requested == UNLIMITED - 1)	/* UNLIMITED-1 means set NF */
			requested = parse_high_water;
	}
	if (parse_high_water < requested) { /* requested beyond end of record */
		if (assign != NULL) {	/* expand record */
			if (requested > nf_high_water)
				grow_fields_arr(requested);

			NF = requested;
			parse_high_water = requested;
		} else
			return &Null_field;
	}

	return &fields_arr[requested];
}

/* set_element --- set an array element, used by do_split() */

static void
set_element(long num, char *s, long len, NODE *n)
{
	NODE *it;
	NODE **lhs;
	NODE *sub;

	it = make_string(s, len);
	it->flags |= USER_INPUT;
	sub = make_number((AWKNUM) (num));
	lhs = assoc_lookup(n, sub);
	unref(*lhs);
	*lhs = it;
        if (n->astore != NULL)
                (*n->astore)(n, sub);
	unref(sub);
}

/* do_split --- implement split(), semantics are same as for field splitting */

NODE *
do_split(int nargs)
{
	NODE *src, *arr, *sep, *fs, *tmp, *sep_arr = NULL;
	char *s;
	long (*parseit)(long, char **, int, NODE *,
			 Regexp *, Setfunc, NODE *, NODE *, bool);
	Regexp *rp = NULL;

	if (nargs == 4) {
		static bool warned1 = false, warned2 = false;

		if (do_traditional || do_posix) {
			fatal(_("split: fourth argument is a gawk extension"));
		}
		sep_arr = POP_PARAM();
		if (sep_arr->type != Node_var_array)
			fatal(_("split: fourth argument is not an array"));
		if (do_lint && ! warned1) {
			warned1 = true;
			lintwarn(_("split: fourth argument is a gawk extension"));
		}
		if (do_lint_old && ! warned2) {
			warned2 = true;
			warning(_("split: fourth argument is a gawk extension"));
		}
	}

	sep = POP();
	arr = POP_PARAM();
	if (arr->type != Node_var_array)
		fatal(_("split: second argument is not an array"));

	if (sep_arr != NULL) {
		if (sep_arr == arr)
			fatal(_("split: cannot use the same array for second and fourth args"));

		/* This checks need to be done before clearing any of the arrays */
		for (tmp = sep_arr->parent_array; tmp != NULL; tmp = tmp->parent_array)
			if (tmp == arr)
				fatal(_("split: cannot use a subarray of second arg for fourth arg"));
		for (tmp = arr->parent_array; tmp != NULL; tmp = tmp->parent_array)
			if (tmp == sep_arr)
				fatal(_("split: cannot use a subarray of fourth arg for second arg"));
		assoc_clear(sep_arr);
	}
	assoc_clear(arr);

	src = TOP_STRING();
	if (src->stlen == 0) {
		/*
		 * Skip the work if first arg is the null string.
		 */
		tmp = POP_SCALAR();
		DEREF(tmp);
		return make_number((AWKNUM) 0);
	}

	if ((sep->flags & REGEX) != 0)
		sep = sep->typed_re;

	if (   (sep->re_flags & FS_DFLT) != 0
	    && current_field_sep() == Using_FS
	    && ! RS_is_null) {
		parseit = parse_field;
		fs = force_string(FS_node->var_value);
		rp = FS_regexp;
	} else {
		fs = sep->re_exp;

		if (fs->stlen == 0) {
			static bool warned = false;

			parseit = null_parse_field;

			if (do_lint && ! warned) {
				warned = true;
				lintwarn(_("split: null string for third arg is a gawk extension"));
			}
		} else if (fs->stlen == 1 && (sep->re_flags & CONSTANT) == 0) {
			if (fs->stptr[0] == ' ') {
				parseit = def_parse_field;
			} else
				parseit = sc_parse_field;
		} else {
			parseit = re_parse_field;
			rp = re_update(sep);
		}
	}

	s = src->stptr;
	tmp = make_number((AWKNUM) (*parseit)(UNLIMITED, &s, (int) src->stlen,
					     fs, rp, set_element, arr, sep_arr, false));

	src = POP_SCALAR();	/* really pop off stack */
	DEREF(src);
	return tmp;
}

/*
 * do_patsplit --- implement patsplit(), semantics are same as for field
 *		   splitting with FPAT.
 */

NODE *
do_patsplit(int nargs)
{
	NODE *src, *arr, *sep, *fpat, *tmp, *sep_arr = NULL;
	char *s;
	Regexp *rp = NULL;

	if (nargs == 4) {
		sep_arr = POP_PARAM();
		if (sep_arr->type != Node_var_array)
			fatal(_("patsplit: fourth argument is not an array"));
	}
	sep = POP();
	arr = POP_PARAM();
	if (arr->type != Node_var_array)
		fatal(_("patsplit: second argument is not an array"));

	src = TOP_STRING();

	if ((sep->flags & REGEX) != 0)
		sep = sep->typed_re;

	fpat = sep->re_exp;
	if (fpat->stlen == 0)
		fatal(_("patsplit: third argument must be non-null"));

	if (sep_arr != NULL) {
		if (sep_arr == arr)
			fatal(_("patsplit: cannot use the same array for second and fourth args"));

		/* These checks need to be done before clearing any of the arrays */
		for (tmp = sep_arr->parent_array; tmp != NULL; tmp = tmp->parent_array)
			if (tmp == arr)
				fatal(_("patsplit: cannot use a subarray of second arg for fourth arg"));
		for (tmp = arr->parent_array; tmp != NULL; tmp = tmp->parent_array)
			if (tmp == sep_arr)
				fatal(_("patsplit: cannot use a subarray of fourth arg for second arg"));
		assoc_clear(sep_arr);
	}
	assoc_clear(arr);

	if (src->stlen == 0) {
		/*
		 * Skip the work if first arg is the null string.
		 */
		tmp =  make_number((AWKNUM) 0);
	} else {
		rp = re_update(sep);
		s = src->stptr;
		tmp = make_number((AWKNUM) fpat_parse_field(UNLIMITED, &s,
				(int) src->stlen, fpat, rp,
				set_element, arr, sep_arr, false));
	}

	src = POP_SCALAR();	/* really pop off stack */
	DEREF(src);
	return tmp;
}

/* set_parser: update the current (non-API) parser */

static void
set_parser(long (*func)(long, char **, int, NODE *, Regexp *, Setfunc, NODE *, NODE *, bool))
{
	save_parse_field = func;
	if (parse_field != api_parse_field && parse_field != func) {
		parse_field = func;
	        update_PROCINFO_str("FS", current_field_sep_str());
	}
}

/* set_FIELDWIDTHS --- handle an assignment to FIELDWIDTHS */

void
set_FIELDWIDTHS()
{
	char *scan;
	char *end;
	int i;
	static int fw_alloc = 4;
	static bool warned = false;
	bool fatal_error = false;
	NODE *tmp;

	if (do_lint && ! warned) {
		warned = true;
		lintwarn(_("`FIELDWIDTHS' is a gawk extension"));
	}
	if (do_traditional)	/* quick and dirty, does the trick */
		return;

	/*
	 * If changing the way fields are split, obey least-suprise
	 * semantics, and force $0 to be split totally.
	 */
	if (fields_arr != NULL)
		(void) get_field(UNLIMITED - 1, 0);

	set_parser(fw_parse_field);
	tmp = force_string(FIELDWIDTHS_node->var_value);
	scan = tmp->stptr;

	if (FIELDWIDTHS == NULL)
		emalloc(FIELDWIDTHS, int *, fw_alloc * sizeof(int), "set_FIELDWIDTHS");
	FIELDWIDTHS[0] = 0;
	for (i = 1; ; i++) {
		unsigned long int tmp;
		if (i + 1 >= fw_alloc) {
			fw_alloc *= 2;
			erealloc(FIELDWIDTHS, int *, fw_alloc * sizeof(int), "set_FIELDWIDTHS");
		}
		/* Initialize value to be end of list */
		FIELDWIDTHS[i] = -1;
		/* Ensure that there is no leading `-' sign.  Otherwise,
		   strtoul would accept it and return a bogus result.  */
		while (is_blank(*scan)) {
			++scan;
		}
		if (*scan == '-') {
			fatal_error = true;
			break;
		}
		if (*scan == '\0')
			break;

		/* Detect an invalid base-10 integer, a valid value that
		   is followed by something other than a blank or '\0',
		   or a value that is not in the range [1..INT_MAX].  */
		errno = 0;
		tmp = strtoul(scan, &end, 10);
		if (errno != 0
		    	|| (*end != '\0' && ! is_blank(*end))
				|| !(0 < tmp && tmp <= INT_MAX)
		) {
			fatal_error = true;
			break;
		}
		FIELDWIDTHS[i] = tmp;
		scan = end;
		/* Skip past any trailing blanks.  */
		while (is_blank(*scan)) {
			++scan;
		}
		if (*scan == '\0')
			break;
	}
	FIELDWIDTHS[i+1] = -1;

	if (fatal_error)
		fatal(_("invalid FIELDWIDTHS value, near `%s'"),
			      scan);
}

/* set_FS --- handle things when FS is assigned to */

void
set_FS()
{
	char buf[10];
	NODE *fs;
	static NODE *save_fs = NULL;
	static NODE *save_rs = NULL;
	bool remake_re = true;

	/*
	 * If changing the way fields are split, obey least-surprise
	 * semantics, and force $0 to be split totally.
	 */
	if (fields_arr != NULL)
		(void) get_field(UNLIMITED - 1, 0);

	/* It's possible that only IGNORECASE changed, or FS = FS */
	/*
	 * This comparison can't use cmp_nodes(), which pays attention
	 * to IGNORECASE, and that's not what we want.
	 */
	if (save_fs
		&& FS_node->var_value->stlen == save_fs->stlen
		&& memcmp(FS_node->var_value->stptr, save_fs->stptr, save_fs->stlen) == 0
		&& save_rs
		&& RS_node->var_value->stlen == save_rs->stlen
		&& memcmp(RS_node->var_value->stptr, save_rs->stptr, save_rs->stlen) == 0) {
		if (FS_regexp != NULL)
			FS_regexp = (IGNORECASE ? FS_re_no_case : FS_re_yes_case);

		/* FS = FS */
		if (current_field_sep() == Using_FS) {
			return;
		} else {
			remake_re = false;
			goto choose_fs_function;
		}
	}

	unref(save_fs);
	save_fs = dupnode(FS_node->var_value);
	unref(save_rs);
	save_rs = dupnode(RS_node->var_value);
	resave_fs = true;

	/* If FS_re_no_case assignment is fatal (make_regexp in remake_re)
	 * FS_regexp will be NULL with a non-null FS_re_yes_case.
	 * refree() handles null argument; no need for `if (FS_regexp != NULL)' below.
	 * Please do not remerge.
	 */
	refree(FS_re_yes_case);
	refree(FS_re_no_case);
	FS_re_yes_case = FS_re_no_case = FS_regexp = NULL;


choose_fs_function:
	buf[0] = '\0';
	default_FS = false;
	fs = force_string(FS_node->var_value);

	if (! do_traditional && fs->stlen == 0) {
		static bool warned = false;

		set_parser(null_parse_field);

		if (do_lint && ! warned) {
			warned = true;
			lintwarn(_("null string for `FS' is a gawk extension"));
		}
	} else if (fs->stlen > 1) {
		if (do_lint_old)
			warning(_("old awk does not support regexps as value of `FS'"));
		set_parser(re_parse_field);
	} else if (RS_is_null) {
		/* we know that fs->stlen <= 1 */
		set_parser(sc_parse_field);
		if (fs->stlen == 1) {
			if (fs->stptr[0] == ' ') {
				default_FS = true;
				strcpy(buf, "[ \t\n]+");
			} else if (fs->stptr[0] == '\\') {
				/* yet another special case */
				strcpy(buf, "[\\\\\n]");
			} else if (fs->stptr[0] == '\0') {
				/* and yet another special case */
				strcpy(buf, "[\\000\n]");
			} else if (fs->stptr[0] != '\n') {
				sprintf(buf, "[%c\n]", fs->stptr[0]);
			}
		}
	} else {
		set_parser(def_parse_field);

		if (fs->stlen == 1) {
			if (fs->stptr[0] == ' ')
				default_FS = true;
			else if (fs->stptr[0] == '\\')
				/* same special case */
				strcpy(buf, "[\\\\]");
			else
				set_parser(sc_parse_field);
		}
	}
	if (remake_re) {
		refree(FS_re_yes_case);
		refree(FS_re_no_case);
		FS_re_yes_case = FS_re_no_case = FS_regexp = NULL;

		if (buf[0] != '\0') {
			FS_re_yes_case = make_regexp(buf, strlen(buf), false, true, true);
			FS_re_no_case = make_regexp(buf, strlen(buf), true, true, true);
			FS_regexp = (IGNORECASE ? FS_re_no_case : FS_re_yes_case);
			set_parser(re_parse_field);
		} else if (parse_field == re_parse_field) {
			FS_re_yes_case = make_regexp(fs->stptr, fs->stlen, false, true, true);
			FS_re_no_case = make_regexp(fs->stptr, fs->stlen, true, true, true);
			FS_regexp = (IGNORECASE ? FS_re_no_case : FS_re_yes_case);
		} else
			FS_re_yes_case = FS_re_no_case = FS_regexp = NULL;
	}

	/*
	 * For FS = "c", we don't use IGNORECASE. But we must use
	 * re_parse_field to get the character and the newline as
	 * field separators.
	 */
	if (fs->stlen == 1 && parse_field == re_parse_field)
		FS_regexp = FS_re_yes_case;
}

/* current_field_sep --- return what field separator is */

field_sep_type
current_field_sep()
{
	if (parse_field == fw_parse_field)
		return Using_FIELDWIDTHS;
	else if (parse_field == fpat_parse_field)
		return Using_FPAT;
	else if (parse_field == api_parse_field)
		return Using_API;
	else
		return Using_FS;
}

/* current_field_sep --- return what field separator is */

const char *
current_field_sep_str()
{
	if (parse_field == fw_parse_field)
		return "FIELDWIDTHS";
	else if (parse_field == fpat_parse_field)
		return "FPAT";
	else if (parse_field == api_parse_field)
		return "API";
	else
		return "FS";
}

/* update_PROCINFO_str --- update PROCINFO[sub] with string value */

void
update_PROCINFO_str(const char *subscript, const char *str)
{
	NODE **aptr;
	NODE *tmp;

	if (PROCINFO_node == NULL)
		return;
	tmp = make_string(subscript, strlen(subscript));
	aptr = assoc_lookup(PROCINFO_node, tmp);
	unref(tmp);
	unref(*aptr);
	*aptr = make_string(str, strlen(str));
}

/* update_PROCINFO_num --- update PROCINFO[sub] with numeric value */

void
update_PROCINFO_num(const char *subscript, AWKNUM val)
{
	NODE **aptr;
	NODE *tmp;

	if (PROCINFO_node == NULL)
		return;
	tmp = make_string(subscript, strlen(subscript));
	aptr = assoc_lookup(PROCINFO_node, tmp);
	unref(tmp);
	unref(*aptr);
	*aptr = make_number(val);
}

/* set_FPAT --- handle an assignment to FPAT */

void
set_FPAT()
{
	static bool warned = false;
	static NODE *save_fpat = NULL;
	bool remake_re = true;
	NODE *fpat;

	if (do_lint && ! warned) {
		warned = true;
		lintwarn(_("`FPAT' is a gawk extension"));
	}
	if (do_traditional)	/* quick and dirty, does the trick */
		return;

	/*
	 * If changing the way fields are split, obey least-suprise
	 * semantics, and force $0 to be split totally.
	 */
	if (fields_arr != NULL)
		(void) get_field(UNLIMITED - 1, 0);

	/* It's possible that only IGNORECASE changed, or FPAT = FPAT */
	/*
	 * This comparison can't use cmp_nodes(), which pays attention
	 * to IGNORECASE, and that's not what we want.
	 */
	if (save_fpat
		&& FPAT_node->var_value->stlen == save_fpat->stlen
		&& memcmp(FPAT_node->var_value->stptr, save_fpat->stptr, save_fpat->stlen) == 0) {
		if (FPAT_regexp != NULL)
			FPAT_regexp = (IGNORECASE ? FPAT_re_no_case : FPAT_re_yes_case);

		/* FPAT = FPAT */
		if (current_field_sep() == Using_FPAT) {
			return;
		} else {
			remake_re = false;
			goto set_fpat_function;
		}
	}

	unref(save_fpat);
	save_fpat = dupnode(FPAT_node->var_value);
	refree(FPAT_re_yes_case);
	refree(FPAT_re_no_case);
	FPAT_re_yes_case = FPAT_re_no_case = FPAT_regexp = NULL;

set_fpat_function:
	fpat = force_string(FPAT_node->var_value);
	set_parser(fpat_parse_field);

	if (remake_re) {
		refree(FPAT_re_yes_case);
		refree(FPAT_re_no_case);
		FPAT_re_yes_case = FPAT_re_no_case = FPAT_regexp = NULL;

		FPAT_re_yes_case = make_regexp(fpat->stptr, fpat->stlen, false, true, true);
		FPAT_re_no_case = make_regexp(fpat->stptr, fpat->stlen, true, true, true);
		FPAT_regexp = (IGNORECASE ? FPAT_re_no_case : FPAT_re_yes_case);
	}
}

/*
 * increment_scan --- macro to move scan pointer ahead by one character.
 * 			Implementation varies if doing MBS or not.
 */

#define increment_scan(scanp, len) incr_scan(scanp, len, & mbs)

/* incr_scan --- MBS version of increment_scan() */

static void
incr_scan(char **scanp, size_t len, mbstate_t *mbs)
{
	size_t mbclen = 0;

	if (gawk_mb_cur_max > 1) {
		mbclen = mbrlen(*scanp, len, mbs);
		if (   (mbclen == 1)
		    || (mbclen == (size_t) -1)
		    || (mbclen == (size_t) -2)
		    || (mbclen == 0)) {
			/* We treat it as a singlebyte character.  */
			mbclen = 1;
		}
		*scanp += mbclen;
	} else
		(*scanp)++;
}

/*
 * fpat_parse_field --- parse fields using a regexp.
 *
 * This is called both from get_field() and from do_patsplit()
 * via (*parse_field)().  This variation is for when FPAT is a regular
 * expression -- use the value to find field contents.
 *
 * This was really hard to get right.  It happens to bear many resemblances
 * to issues I had with getting gsub right with null matches. When dealing
 * with that I prototyped in awk and had the foresight to save the awk code
 * over in the C file.  Starting with that as a base, I finally got to this
 * awk code to do what I needed, and then translated it into C. Fortunately
 * the C code bears a closer correspondance to the awk code here than over
 * by gsub.
 *
 * BEGIN {
 * 	false = 0
 * 	true = 1
 *
 * 	fpat[1] = "([^,]*)|(\"[^\"]+\")"
 * 	fpat[2] = fpat[1]
 * 	fpat[3] = fpat[1]
 * 	fpat[4] = "aa+"
 * 	fpat[5] = fpat[4]
 *
 * 	data[1] = "Robbins,,Arnold,"
 * 	data[2] = "Smith,,\"1234 A Pretty Place, NE\",Sometown,NY,12345-6789,USA"
 * 	data[3] = "Robbins,Arnold,\"1234 A Pretty Place, NE\",Sometown,NY,12345-6789,USA"
 * 	data[4] = "bbbaaacccdddaaaaaqqqq"
 * 	data[5] = "bbbaaacccdddaaaaaqqqqa" # should get trailing qqqa
 *
 * 	for (i = 1; i in data; i++) {
 * 		printf("Splitting: <%s>\n", data[i])
 * 		n = mypatsplit(data[i], fields, fpat[i], seps)
 * 		print "n =", n
 * 		for (j = 1; j <= n; j++)
 * 			printf("fields[%d] = <%s>\n", j, fields[j])
 * 		for (j = 0; j in seps; j++)
 * 			printf("seps[%s] = <%s>\n", j, seps[j])
 * 	}
 * }
 *
 * function mypatsplit(string, array, pattern, seps,
 * 			eosflag, non_empty, nf) # locals
 * {
 * 	delete array
 * 	delete seps
 * 	if (length(string) == 0)
 * 		return 0
 *
 * 	eosflag = non_empty = false
 * 	nf = 0
 * 	while (match(string, pattern)) {
 * 		if (RLENGTH > 0) {	# easy case
 * 			non_empty = true
 * 			if (! (nf in seps)) {
 * 				if (RSTART == 1)	# match at front of string
 * 					seps[nf] = ""
 * 				else
 * 					seps[nf] = substr(string, 1, RSTART - 1)
 * 			}
 * 			array[++nf] = substr(string, RSTART, RLENGTH)
 * 			string = substr(string, RSTART+RLENGTH)
 * 			if (length(string) == 0)
 * 				break
 * 		} else if (non_empty) {
 * 			# last match was non-empty, and at the
 * 			# current character we get a zero length match,
 * 			# which we don't want, so skip over it
 * 			non_empty = false
 * 			seps[nf] = substr(string, 1, 1)
 * 			string = substr(string, 2)
 * 		} else {
 * 			# 0 length match
 * 			if (! (nf in seps)) {
 * 				if (RSTART == 1)
 * 					seps[nf] = ""
 * 				else
 * 					seps[nf] = substr(string, 1, RSTART - 1)
 * 			}
 * 			array[++nf] = ""
 * 			if (! non_empty && ! eosflag) { # prev was empty
 * 				seps[nf] = substr(string, 1, 1)
 * 			}
 * 			if (RSTART == 1) {
 * 				string = substr(string, 2)
 * 			} else {
 * 				string = substr(string, RSTART + 1)
 * 			}
 * 			non_empty = false
 * 		}
 * 		if (length(string) == 0) {
 * 			if (eosflag)
 * 				break
 * 			else
 * 				eosflag = true
 * 		}
 * 	}
 * 	if (length(string) > 0)
 * 		seps[nf] = string
 *
 * 	return length(array)
 * }
 */
static long
fpat_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n,
	NODE *sep_arr,  /* array of field separators (may be NULL) */
	bool in_middle)
{
	char *scan = *buf;
	long nf = parse_high_water;
	char *start;
	char *end = scan + len;
	int regex_flags = RE_NEED_START;
	bool need_to_set_sep;
	bool non_empty;
	bool eosflag;
	mbstate_t mbs;

	memset(&mbs, 0, sizeof(mbstate_t));

	if (up_to == UNLIMITED)
		nf = 0;

	if (len == 0)
		return nf;

	if (rp == NULL) /* use FPAT */
		rp = FPAT_regexp;

	if (in_middle) {
		regex_flags |= RE_NO_BOL;
	}
	non_empty = rp->non_empty;

	eosflag = false;
	need_to_set_sep = true;
	start = scan;
	while (research(rp, scan, 0, (end - scan), regex_flags) != -1
	       && nf < up_to) {

		if (REEND(rp, scan) > RESTART(rp, scan)) { /* if (RLENGTH > 0) */
			non_empty = true;
			if (sep_arr != NULL && need_to_set_sep) {
				if (RESTART(rp, scan) == 0) /* match at front */
		    			set_element(nf, start, 0L, sep_arr);
				else
		    			set_element(nf,
						start,
						(long) RESTART(rp, scan),
						sep_arr);
			}
			/* field is text that matched */
			(*set)(++nf,
				scan + RESTART(rp, scan),
				(long)(REEND(rp, scan) - RESTART(rp, scan)),
				n);

			scan += REEND(rp, scan);
			if (scan >= end)
				break;
			need_to_set_sep = true;
		} else if (non_empty) { /* else if non_empty */
			/*
			 * last match was non-empty, and at the
			 * current character we get a zero length match,
			 * which we don't want, so skip over it
			 */
			non_empty = false;
			if (sep_arr != NULL) {
				need_to_set_sep = false;
		    		set_element(nf, start, 1L, sep_arr);
			}
			increment_scan(& scan, end - scan);
		} else {
			/* 0 length match */
			if (sep_arr != NULL && need_to_set_sep) {
				if (RESTART(rp, scan) == 0) /* RSTART == 1 */
		    			set_element(nf, start, 0L, sep_arr);
				else
		    			set_element(nf, start,
							(long) RESTART(rp, scan),
							sep_arr);
			}
			need_to_set_sep = true;
			(*set)(++nf, scan, 0L, n);
			if (! non_empty && ! eosflag) { /* prev was empty */
				if (sep_arr != NULL) {
		    			set_element(nf, start, 1L, sep_arr);
					need_to_set_sep = false;
				}
			}
			if (RESTART(rp, scan) == 0)
				increment_scan(& scan, end - scan);
			else {
				scan += RESTART(rp, scan);
			}
			non_empty = false;
		}
		if (scan >= end) { /* length(string) == 0 */
			if (eosflag)
				break;
			else
				eosflag = true;
		}

		start = scan;
	}
	if (scan < end) {
		if (sep_arr != NULL)
    			set_element(nf, scan, (long) (end - scan), sep_arr);
	}

	*buf = scan;
	rp->non_empty = non_empty;
	return nf;
}
