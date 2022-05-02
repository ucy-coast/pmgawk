#!/usr/bin/gawk -f

{ 
  for(i=1;i<=NF;i++) 
    n[$i]++; 
}

END {
  for (s in n)
    print s, n[s];
}
