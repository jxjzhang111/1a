#foo || (wc && sort && wc -l) < input.txt > output.txt
ls -l > test.txt
ps | grep bash | sort
(echo b; echo c; echo a) | sort
foo || echo failure to foo
echo fooing && foo
foo && echo shouldnotrun
sort < test.txt