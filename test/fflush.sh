#! /bin/sh
$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"cat"}'

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"cat"}'|cat

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");close("/dev/stdout");print "2nd"|"cat"}'|cat

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"cat";close("cat")}'|cat

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"cat";close("cat")}'|cat

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"cat";close("cat")}'|cat

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"sort"}'|cat

$AWK 'BEGIN{print "1st";fflush("/dev/stdout");print "2nd"|"sort";close("sort")}'|cat
