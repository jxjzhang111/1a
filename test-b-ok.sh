#! /bin/sh

# UCLA CS 111 Lab 1 - Test that valid syntax is processed correctly.

tmp=$0-$$.tmp
mkdir "$tmp" || exit

(
cd "$tmp" || exit

cat >test.sh <<'EOF'
(echo b; echo c; echo a) | sort

echo Lorem ipsum dolor sit amet > testfile

sort < testfile
EOF

cat >test.exp <<'EOF'
a
b
c
Lorem ipsum dolor sit amet
EOF

../timetrash test.sh >test.out 2>test.err || exit

diff -u test.exp test.out || exit
test ! -s test.err || {
  cat test.err
  exit 1
}

) || exit

rm -fr "$tmp"
