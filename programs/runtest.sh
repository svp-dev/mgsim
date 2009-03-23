#! /bin/sh
set -e
ARCH=${1:?}
NRCPUS=${2:?}
TEST=${3:?}
sim=${MGSIM:?}

fail=0

dotest() {
  local i extraarg extradesc
  extraarg=$1
  extradesc=$2
  i=1
  while test $i -le $NRCPUS; do
      cmd="$sim -o NumProcessors=$i $extraarg $TEST"
      printf "%s" "TEST: $TEST: CPUS=$i $extradesc -> "
      set +e
      $cmd >/dev/null 2>&1
      x=$?
      set -e
      if test $x = 0; then
	  echo "PASS"
      else
	  if test $x -ge 126; then
              exit $x
	  fi
	  echo "FAIL"
	  fail=1
      fi
      
      i=$(expr $i + 1)
  done
}

rdata=$(strings <"$TEST"|grep "TEST_INPUTS"|head -n1)
if test -n "$rdata"; then
    reg=$(echo "$rdata"|cut -d: -f2)
    vals=$(echo "$rdata"|cut -d: -f3)
    for val in $vals; do
	dotest "-$reg $val" "$reg=$val"
    done
else
    dotest "" ""
fi


exit $fail


