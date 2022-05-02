#!/bin/bash

# Configuration parameters
source config.sh

# Cleanup
rm -f good_mail-gawk spam_mail-gawk
rm -f good_mail-pmgawk spam_mail-pmgawk
rm -f spamprobability

# Train
./train-gawk.sh
./train-pmgawk.sh

# Filter
for I in test/*
do
  echo $I
  bash filter-gawk.sh $I
done

for I in test/*
do
  echo $I
  bash filter-pmgawk.sh $I
done

md5sum good_mail-gawk
md5sum good_mail-pmgawk
md5sum spam_mail-gawk
md5sum spam_mail-pmgawk
