  Copyright (C) 2005, 2006, 2007, 2009, 2010, 2011, 2012, 2013, 2014, 2015,
  2016, 2017, 2018, 2019, 2020, 2021 Free Software Foundation, Inc.
  
  Copying and distribution of this file, with or without modification,
  are permitted in any medium without royalty provided the copyright
  notice and this notice are preserved.

README:

This is GNU Awk 5.1.1. It is upwardly compatible with Brian Kernighan's
version of Unix awk.  It is almost completely compliant with the
2018 POSIX 1003.1 standard for awk. (See the note below about POSIX.)

This is a bug-fix release. See NEWS and ChangeLog for details.

Work to be done is described briefly in the TODO file, which is available
only in the 'master' branch in the Git repo.

Changes in this version are summarized in the NEWS file.

Read the file POSIX.STD for a discussion of issues where the standard
says one thing but gawk does something different.

To format the documentation with TeX, use at least version 2019-09-20.22
of texinfo.tex. There is a usable copy of texinfo.tex in the doc directory.
You must also use at least version 6.7 of texindex and of makeinfo
from the texinfo-6.7 distribution.

INSTALLATION:

Check whether there is a system-specific README file for your system under
the `README_d' directory.  If there's something there that you should
have read and didn't, and you bug me about it, I'm going to yell at you.

See the file INSTALL for installation instructions.

If you don't have Bison, use the awkgram.c file here.  It was
generated with Bison, and has no proprietary code in it.  (Note that
modifying awkgram.y without Bison is next to impossible.  You might
want to get a copy of Bison from the FSF too.)

The build mechanics depend upon Bison. Also, gawk doesn't work correctly
with some versions of yacc, so just use Bison.

If you have an MS-DOS, MS-Windows, or OS/2 system, use the stuff in the `pc'
directory.  Similarly, there is a separate directory for VMS.

Appendix B of ``GAWK: Effective Awk Programming'' discusses configuration
in detail. The configuration process is based on GNU Autoconf and
Automake.

After successful compilation, do `make check' to run the test suite.
There should be no output from the `cmp' invocations except in the
cases where there are small differences in floating point values, and
possibly in the case of strftime.  There may be differences based on
installed (or not installed) locales and the quality of multibyte
character support on your system.

Several of the tests ignore errors on purpose; those are not a problem.
If there are other differences, please investigate and report the problem.

PRINTING THE MANUAL

The `doc' directory contains a recent version of texinfo.tex, which will
be necessary for printing the manual.  Use `make dvi' to get a DVI file
from the manual. In the `doc' directory, use `make postscript' to get
PostScript versions of the manual, the man page, and the reference card.
Use `make pdf' to get PDF versions of the manuals, the man page and
the reference card.

BUG REPORTS AND FIXES (Un*x systems):

Please coordinate changes through Arnold Robbins. In particular, see
the section in the manual on reporting bugs. Note that comp.lang.awk
is about the worst place to post a gawk bug report. So too is use of
a web forum such as Stack Overflow. Please, use the mechanisms outlined
in the manual.

Bug reports should be sent to bug-gawk@gnu.org.  This is a separate mailing
list at GNU Central.  The advantage to using this address is that bug
reports are archived at GNU Central.

General non-bug questions should be sent to help-gawk@gnu.org.

Arnold Robbins

BUG REPORTS AND FIXES, non-Unix systems:

MS-DOS with DJGPP:
	Juan Manuel Guerrero
	juan.guerrero@gmx.de

MS-Windows with MinGW:
	Eli Zaretskii
	eliz@gnu.org

OS/2:
	Andreas Buening
	andreas.buening@nexgo.de

VMS:
	John Malmberg
	wb8tyw@qsl.net

z/OS (OS/390) Contact:
	Daniel Richard G.
	skunk@iSKUNK.ORG
