#!/usr/bin/python

import optparse, sys, os, signal, time, popen2, socket

def run_test(index, options):
    index = str(index)
    
    # Construct the argument list
    args = [options.executable, "-c", options.config, "-p", str(index)]

    if options.overrides is not None:
        # Configuration overrides
        for o in options.overrides:
            args.extend(["-o", o + "=" + index])

    if options.register is not None:
        # Register override
        args.extend(["-" + options.register, index])
    
    if options.arguments is not None:
        # Arguments
        args.extend( options.arguments.split() )
        
    args.append(options.program)
    
    hostname = socket.gethostname()

    p = None
    retval = 0
    try:
        # Start process
        p = popen2.Popen4(" ".join(args))

        # Wait for it
        p.wait()
    except:
        # Something interrupted us, kill the process
        exc = sys.exc_info()
        print "[%s] %s: %s" % (hostname, exc[0].__name__, exc[1])
        
        if p is not None:
            os.kill(p.pid, signal.SIGKILL)
        retval = 1

    # Read, format and print output        
    output = p.fromchild.read().strip().split("\n")
    p.fromchild.close()
    for line in output:
        print "[%s] %s" % (hostname, line)
    
    return retval

def main(argv = None):
    if argv is None:
        argv = sys.argv
    
    # Parse options
    parser = optparse.OptionParser(usage="usage: %prog [options] start limit step")
    parser.add_option("-e", "--exec", dest="executable",
        help="Specifies the executable file", metavar="FILE")
    parser.add_option("-c", "--config", dest="config",
        help="Specifies the configuration file", metavar="FILE")
    parser.add_option("-p", "--program", dest="program",
        help="Specifies the program file", metavar="FILE")
    parser.add_option("-a", "--arg", dest="arguments",
        help="Pass the specified arguments on to the program", metavar="ARGS")
    parser.add_option("-o", "--override", action="append", dest="overrides",
        help="Overrides the specified configuration option with the current index", metavar="OPTION")
    parser.add_option("-r", "--reg", dest="register",
        help="Overrides the specified register with the current index", metavar="REG")
    options, args = parser.parse_args()
    
    # Parse aruments
    try:
        start = int(args[0])
        limit = int(args[1])
        step  = int(args[2])
    except (IndexError, ValueError):
        print "Expected three numeric arguments"
        return 1
    
    if options.executable is None:
        print "Specify an executable file"
        return 1

    if options.config is None:
        print "Specify a configuration file"
        return 1

    if options.program is None:
        print "Specify a program file"
        return 1
    
    for i in range(start, limit + 1, step):
        exit = run_test(i, options)
        if exit != 0:
            return exit
    return 0
    
if __name__ == '__main__':
    sys.exit(main())
