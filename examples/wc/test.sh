#!/bin/bash

HEAPFILE=/tmp/heap.pma
HEAPSIZE=`echo "1*1024*4096" | bc -l`

# clean up from previous runs
rm -f out.gawk
rm -f out.pmgawk
rm -f $HEAPFILE
 
# create heap backing file
if (( $(echo "scale=0; $HEAPSIZE % 4096" | bc -l) != 0 )); then
  echo "Heap size $HEAPSIZE must be multiple of page size 4096"
  exit
fi
truncate $HEAPFILE --size $HEAPSIZE

# process one input per awk invocation 
$PMGAWK --persist=$HEAPFILE -f ./wc.awk input0.txt > out.pmgawk
$PMGAWK --persist=$HEAPFILE -f ./wc.awk input1.txt > out.pmgawk
$PMGAWK --persist=$HEAPFILE -f ./wc.awk input2.txt > out.pmgawk

# process all inputs in a single invocation 
$GAWK -f ./wc.awk input0.txt input1.txt input2.txt > out.gawk

# compare outputs
md5sum out.gawk
md5sum out.pmgawk
