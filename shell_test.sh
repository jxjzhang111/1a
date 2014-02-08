#foo || (wc && sort && wc -l) < input.txt > output.txt
#ls -a > test.txt ; echo asdf > test2.txt ; echo asdf > test3.txt; echo asdf > test4.txt;
#echo fds > a1.txt; echo fds > a3.txt; echo fds > a1.txt; echo fds > a5.txt;
#sort < test.txt; diff a3.txt test3.txt;
#ps | grep bash | sort
#(echo b; echo c; echo a) | sort
#foo || echo failure to foo
#echo fooing && foo
#foo && echo shouldnotrun
#sort < test.txt
echo a b c > test1.txt; sleep 1
sort < test1.txt
echo c