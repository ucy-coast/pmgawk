#! /bin/sh
IFS=
PMEM=$(mktemp /tmp/pmem.XXXXXXXXXX)
truncate -s 40960000 $PMEM
exec ../gawk --persist=$PMEM $@
