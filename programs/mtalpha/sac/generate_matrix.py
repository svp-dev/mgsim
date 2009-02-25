#!/usr/bin/python
import sys, random, optparse

def main(argv = None):
    if argv == None:
        argv = sys.argv

    # Parse options
    parser = optparse.OptionParser(usage="usage: %prog [options] method N D")
    options, args = parser.parse_args()

    # Parse arguments
    try:
        method =  args[0]  # Generation method
        N = int  (args[1]) # Matrix size
        D = float(args[2]) # Dynamic range
    except (IndexError, ValueError):
        print "Expected three valid arguments"
        return 1

    # Same seed so we get deterministic results
    random.seed(0)
    
    if method == "random":
        # Every element is a random element
        for i in range(1,N+1):
            for j in range(1,N+1):
                print "    .double", random.random() * D

    elif method == "flat":
        # Every element is the same
        for i in range(1,N+1):
            for j in range(1,N+1):
                print "    .double", D
                
    elif method == "plane":
        # We have a plane going from (1,1) to (N,N), normalized
        for i in range(1,N+1):
            for j in range(1,N+1):
                print "    .double", (i + j) / (2.0*N) * D
    else:
        print "Invalid generation method. Must be \"random\" or \"plane\""
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
