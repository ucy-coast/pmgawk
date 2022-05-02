#!/bin/bash

curl https://spamassassin.apache.org/old/publiccorpus/20021010_easy_ham.tar.bz2 | tar -xjv
curl https://spamassassin.apache.org/old/publiccorpus/20021010_spam.tar.bz2 | tar -xjv 

mkdir -p test
cp easy_ham/000* test/ 
cp spam/000* test/ 
