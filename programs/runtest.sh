#! /bin/sh
set -e
ARCH=${1:?}
NRCPUS=${2:?}
TEST=${4:?}
sim=${MGSIM:?}
timeout=${3:?}

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
      exec 3>&2 2>/dev/null
      $timeout $cmd >/dev/null
      x=$?
      exec 2>&3
      set -e
      if test $x = 0; then
	    echo "PASS"
      else
        if test $x = 137; then
            echo "TIMEOUT"
        elif test $x -ge 126; then
            exit $x
	    else
    	    echo "FAIL"
	    fi
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


