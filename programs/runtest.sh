#! /bin/bash
set -e
ARCH=${1:?}
TEST=${3:?}
sim=${MGSIM:?}
simx=${SIMX:?}
timeout=${2:?}

fail=0

dotest() {
  local i extraarg extradesc
  extraarg=$1
  extradesc=$2
  thesim=$3
  i=1
  for i in $cpuconf; do
      cmd="$thesim $SIMARGS -o NumProcessors=$i $extraarg $TEST"
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
        elif test $x = 134; then
            echo "ABORT"
        elif test $x -ge 128; then
            kill -$(($x - 128)) $$
	    else
    	    echo "FAIL"
	    fi
	    fail=1
      fi
      
  done
}

domemtest() {
    for mem in SERIAL PARALLEL BANKED RANDOMBANKED COMA; do
        dotest "-o MemoryType=$mem $1" "MemType=$mem $2" "$sim"
    done
    if $simx --version >/dev/null 2>&1; then
        dotest "$1" "MemType=COMA_ZL $2" "$simx"
    fi
}

rdata=$(strings <"$TEST"|grep "TEST_INPUTS"|head -n1)
pdata=$(strings <"$TEST"|grep "PLACES"|head -n1|cut -d: -f2-)
if test -n "$pdata"; then
  cpuconf=$(eval "echo $pdata")
else
  cpuconf="1 2 3 4"
fi

if test -n "$rdata"; then
    reg=$(echo "$rdata"|cut -d: -f2)
    vals=$(echo "$rdata"|cut -d: -f3)
    for val in $vals; do
	domemtest "-$reg $val" "$reg=$val" 
    done
else
    domemtest "" ""
fi

exit $fail

