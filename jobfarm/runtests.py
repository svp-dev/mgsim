#!/usr/bin/python

import optparse, sys, os, signal, time, status

def run_test(host, runtest, options, start, limit, step):
    
    # Construct the argument list
    args = ["rsh", "-n", host, runtest,
            "-e", options.executable, "-c", options.config, "-p", options.program]

    if options.overrides is not None:
        # Configuration overrides
        for o in options.overrides:
            args.extend(["-o", o])

    if options.register is not None:
        # Register override
        args.extend(["-r", options.register])
    
    if options.arguments is not None:
        # Arguments
        args.extend(["-a", options.arguments])
        
    args.extend([str(start), str(limit), str(step)])
    
    args = map(lambda x: (x.find(" ") == -1 and x) or '"'+x+'"', args)
            
    # Start process
    try:
        return os.spawnvp(os.P_NOWAIT, "rsh", args)
    except OSError, (errno, strerror):
        print "Error:", strerror
    return None

def main(argv = None):
    if argv is None:
        argv = sys.argv

    # Get the path of the tester application
    runtest = os.path.dirname(os.path.abspath(argv[0])) + "/runtest.py"
    
    # Parse options
    parser = optparse.OptionParser(usage="usage: %prog [options] start limit step")
    parser.add_option("-f", "--file", dest="filename",
        help="Read hosts from FILE", metavar="FILE", default="hosts")
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
        
    # Check hosts status
    hosts = status.check_status(options.filename)
    
    # Do distribution
    retval = 0
    pids   = []
    try:
        print "Distributing jobs..."
        for host in hosts:
            pid = run_test(host, runtest, options, start, limit, step * len(hosts))
            if pid is None:
                retval = 1
                break
            pids.append(pid)
            start = start + step

        print "Distributed jobs, waiting for completion..."
        while len(pids) > 0:
            os.waitpid(pids.pop(), 0)
    except:
        print "Interrupted"
        retval = 1

    # Kill all pids
    for pid in pids:
        os.kill(pid, signal.SIGKILL)

    return retval
    
if __name__ == '__main__':
    sys.exit(main())
