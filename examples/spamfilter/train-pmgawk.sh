#!/bin/bash
# Training script for the SpamFilter.
# Call from the command line or in a crontab file.

# Configuration parameters
source config.sh

rm -f $HEAPFILE; truncate $HEAPFILE --size $HEAPSIZE

number_of_tokens (){
  cat $1/* | wc -w
}

# Create a hash with probability of spaminess per token.
#       Words only in good hash get .01, words only in spam hash get .99
spaminess () {
  $PMGAWK --persist=$HEAPFILE '
    BEGIN {goodnum=ENVIRON["GOODNUM"]; junknum=ENVIRON["JUNKNUM"];}
    END {
      for (word in good_hash) {
          if (word in bad_hash) { spam_probability[word] = \
              (bad_hash[word]/junknum)/ \
              ((good_hash[word]/goodnum)+(bad_hash[word]/junknum)) }
          else { spam_probability[word] = 0.01}
      }
      for (word in bad_hash) {
          if (word in good_hash) { done="already"}
          else { spam_probability[word] = 0.99}
      }
    }
    '
}

frequency_bad (){
  $PMGAWK --persist=$HEAPFILE ' { for (i = 1; i <= NF; i++)
        bad_hash[$i]++ }
    '
}

frequency_good (){
  $PMGAWK --persist=$HEAPFILE ' { for (i = 1; i <= NF; i++)
        good_hash[$i]++ }
    '
}

prepare_data () {
  export JUNKNUM=$(number_of_tokens 'spam')
  export GOODNUM=$(number_of_tokens 'easy_ham')

  echo $JUNKNUM $GOODNUM

  cat ./spam/* | frequency_bad
  cat ./easy_ham/* | frequency_good

  echo "" | spaminess
}

prepare_data
