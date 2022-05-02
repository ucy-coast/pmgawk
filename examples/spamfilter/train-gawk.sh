#!/bin/bash
# Training script for the SpamFilter.
# Call from the command line or in a crontab file.

GAWK=gawk

number_of_tokens (){
  cat $1/* | wc -w
}

# Create a hash with probability of spaminess per token.
#       Words only in good hash get .01, words only in spam hash get .99
spaminess () {
  $GAWK 'BEGIN {goodnum=ENVIRON["GOODNUM"]; junknum=ENVIRON["JUNKNUM"];}
        FILENAME ~ "spamwordfrequency" {bad_hash[$1]=$2}
        FILENAME ~ "goodwordfrequency" {good_hash[$1]=$2}

      END    {
      for (word in good_hash) {
          if (word in bad_hash) { print word, 
              (bad_hash[word]/junknum)/ \
              ((good_hash[word]/goodnum)+(bad_hash[word]/junknum)) }
          else { print word, "0.01"}
      }
      for (word in bad_hash) {
          if (word in good_hash) { done="already"}
          else { print word, "0.99"}
      }}' spamwordfrequency goodwordfrequency 
}

frequency (){
  $GAWK ' { for (i = 1; i <= NF; i++)
        freq[$i]++ }
    END    {
    for (word in freq){
        if (freq[word] > 2) {
          printf "%s\t%d\n", word, freq[word];
        }
    } 
  }'
}

prepare_data () {
  export JUNKNUM=$(number_of_tokens 'spam')
  export GOODNUM=$(number_of_tokens 'easy_ham')

  echo $JUNKNUM $GOODNUM

  cat ./spam/* |
    frequency|
    sort -nr -k 2,2 > spamwordfrequency
  cat ./easy_ham/* |
    frequency|
    sort -nr -k 2,2 > goodwordfrequency

  spaminess| 
    sort -nr -k 2,2 > spamprobability
  
  # Clean up files
  rm spamwordfrequency goodwordfrequency 
}

prepare_data