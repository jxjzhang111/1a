#! /bin/sh

# UCLA CS 111 Lab 1 - Test that valid syntax is processed correctly.

tmp=$0-$$.tmp
mkdir "$tmp" || exit

(
cd "$tmp" || exit

cat >test.sh <<'EOF'
(echo b; echo c; echo a) | sort

sort < ../README
EOF

cat >test.exp <<'EOF'
a
b
c


A semicolon after a complete command at the end of the file is ignored (interpreted as end of command).
Jennifer Zhang (504356174) and Yi-An Lai (304271741)
Semicolons interspersed in the input .sh file are always interpreted as SEQUENCE_COMMANDS.
This is a skeleton for CS 111 Lab 1.
EOF

../timetrash test.sh >test.out 2>test.err || exit

diff -u test.exp test.out || exit
test ! -s test.err || {
  cat test.err
  exit 1
}

) || exit

rm -fr "$tmp"
