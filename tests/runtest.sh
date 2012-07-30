#! /bin/bash
set -e
sim=${1:?}
timeout=${2:?}
cfg1=${3:?}
cfg2=${4:?}
TESTd=${5:?}

fail=0

dotest() {
  local i extraarg extradesc
  extraarg=$1
  extradesc=$2
  thesim=$3
  i=1
  for i in $cpuconf; do
      cmd="$thesim $SIMARGS -o NumProcessors=$i $extraarg $TEST"
      echo "- \`\`$cmd\`\`"
      printf "%s %s" "  " "=> "
      set +e
      exec 3>&2 4>&1 >"$$.out" 2>&1
      $timeout $cmd 
      x=$?
      exec 2>&3 1>&4
      set -e
      rekill=
      if test $x = 0; then
	    echo "**PASS**"
      else
        if test $x = 137; then
            echo "**TIMEOUT**"
        elif test $x = 134; then
            echo "**ABORT**"
        elif test $x -ge 128; then
            echo "**SIGNAL ($x)**"
            rekill=1
	else
    	    echo "**FAIL**"
	fi
        printf "\n  Command line::\n\n  %s\n\n  Output::\n\n" "$cmd"
        sed -e 's/^/    /g' < "$$.out"
        printf "\n  Exit status: %d\n\n" $x
	fail=1
      fi
      rm -f "$$.out"

      if test -n "$rekill"; then
          kill -$(($x - 128)) $$
      fi
  done
}

TEST=$(cat "$TESTd")

rdata=$(strings <"$TEST"|grep "TEST_INPUTS"|head -n1)
pdata=$(strings <"$TEST"|grep "PLACES"|head -n1|cut -d: -f2-)
if test -n "$pdata"; then
  cpuconf=$(eval "echo $pdata")
else
  cpuconf="1 2 4 8"
fi

mem=${TESTd##*.}

for cfg in "$cfg1" "$cfg2"; do
 cfgname=`basename "$cfg"`
 if test -n "$rdata"; then
    reg=$(echo "$rdata"|cut -d: -f2)
    vals=$(echo "$rdata"|cut -d: -f3)
    for val in $vals; do
	dotest "-c $cfg -t -o MemoryType=$mem -$reg $val" "config=$cfgname MemType=$mem $reg=$val" "$sim"
    done
 else
    dotest "-c $cfg -t -o MemoryType=$mem" "MemType=$mem" "$sim"
 fi
done
exit $fail

