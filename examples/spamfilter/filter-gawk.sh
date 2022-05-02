#!/bin/bash
# Spamfilter using statistical filtering.
# Inspired by the Paul Graham article "A Plan for Spam" www.paulgraham.com
#
# If mail is spam then put in a spam file
# else put in the good mail file. 

spamly () {
  cat $1 |
  $GAWK '
    { message[k++]=$0; }

    END { if (k==0) {exit;} # empty message or was in the whitelist.

          good_mail_file="good_mail-gawk";
          spam_mail_file="spam_mail-gawk";
          spam_probability_file="spamprobability";
          total_tokens=0.01;

          while (getline < spam_probability_file)
            bad_hash[$1]=$2; close(spam_probability_file);

          for (line in message){ 
            token_number=split(message[line],tokens);
            for (i = 0; i <= token_number; i++){
              if (tokens[i] in bad_hash) { 
                if (bad_hash[tokens[i]] <= 0.06 || bad_hash[tokens[i]] >= 0.94){
                  total_tokens+=1;
                  spamtotal+=bad_hash[tokens[i]];
                }
              }
            }
          }

          if (spamtotal/total_tokens > 0.50) { 
            for (j = 0; j <= k; j++){ print message[j] >> spam_mail_file}
            print "\n\n" >> spam_mail_file;
          }
          else {
            for (j = 0; j <= k; j++){ print message[j] >> good_mail_file}
            print "\n\n" >> good_mail_file;
          }
    }'
}

spamly $1
