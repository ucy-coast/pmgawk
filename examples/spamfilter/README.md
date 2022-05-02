Statistical Spam Filter 
=======================

This directory contains the implementation of a statistical spam filter 
for use with persistent gawk.

The implementation is based on an existing implementation of a Spam Filter 
for regular AWK available below:

http://216.92.26.41/article/Statistical_spam_filter.html

Noted changes include the following:
- replaced intermediate files used for storing the frequency of good 
  and bad (junk) words with persistent hash tables
- replaced intermediate file used for storing the spam probability of 
  each word with a persistent spam probability table
- added BEGIN blocks to reset global persistent variables and arrays  

Evaluation  
----------

First, set GAWK and PMGAWK environment variables to point 
to the GAWK and PM-GAWK binaries, respectively. For example, in bash:

```
export GAWK=<path to gawk binary>
export PMGAWK=<path to pmgawk binary>
```

For testing, we will use the SpamAssassin public mail corpus, a selection 
of mail messages suitable for use in testing spam filtering systems:

https://spamassassin.apache.org/old/publiccorpus/

To deploy the dataset, use the provided spamassasin.sh script. The script 
will download the dataset and populate the following directories:
- easy_ham: good email files for training 
- spam: junk email files for training
- test: good and junk email files for testing 

Note: The test directory will contain a small subset of easy_ham and spam. 
In general, when training models, using the same dataset for training and 
testing shall be avoided. However, for testing correctness, that is whether 
persistent gawk produces the same result as regular gawk, should be okay.

To train the spamfilter:

```
./train-pmgawk.sh
```

To filter a message:

```
./filter-pmgawk.sh <path to message file>
```

To run the complete test:

```
./test.sh
```
