#! /bin/sh

# UCLA CS 111 Lab 1c - Test that parallel execution is working.

tmp=$0-$$.tmp
mkdir "$tmp" || exit

(
cd "$tmp" || exit

cat >test.sh <<'EOF'
sleep 5 && echo world;
echo hello ; sleep 2 && echo to > t.out ; sleep 1 && echo the > t2.out
cat t.out
sort t2.out | cat

EOF

cat >test.exp <<'EOF'
hello
to
the
world
EOF

../timetrash -t test.sh >test.out 2>test.err || exit

diff -u test.exp test.out || exit
test ! -s test.err || {
  cat test.err
  exit 1
}

) || exit

rm -fr "$tmp"
