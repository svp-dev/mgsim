#!/usr/bin/python
import sys, socket, os, optparse, threading, signal, status

def start_client(host, client, client_args, server, port):

    # Construct the argument list
    args = ["rsh", "-n", host, client]

    args.extend(client_args)
    args.extend([server, str(port)])

    # Start process
    try:
        return os.spawnvp(os.P_NOWAIT, args[0], args)
    except OSError, (errno, strerror):
        print "Could not start client:", strerror
    return None

class ThreadData:
    def __init__(self, start, limit, step, event):
        self.start = start
        self.limit = limit
        self.step  = step
        self.event = event

def conn_thread(s, tdata):
    try:
        work = []
        for i in range(tdata.start, tdata.limit + 1, tdata.step):
            work.append( [i, False, ''] )
    
        next = 0
        done = 0
        s.settimeout(1.0)
        while not tdata.event.isSet():
            try:
                (data, address) = s.recvfrom(1024)
                sys.stdout.flush()
                # Seperate into command and data
                data    = data.split(":");
                command = data[0]
                data    = data[1:]
        
                if command == "I CAN HAS DATA?":
                    # Determine next index to give this client
                    if done >= len(work):
                        # No more work
                        s.sendto("KTHXBAI", address)
                    else:
                        # Find an iteration which hasn't been completed yet
                        while work[next][1]:
                            next = (next + 1) % len(work)
                        # Give it, and move to next
                        s.sendto(":".join(["YU MAEK", str(next), str(work[next][0])]), address)
                        next = (next + 1) % len(work)

                elif command == "SRSLY" and len(data) >= 2:
                    # Work has been completed
                    id     = int(data[0])
                    output = ":".join(data[1:])

                    if work[id][1]:
                        # Work has already been done, compare!
                        if work[id][2] != output:
                            # Whoa, this shouldn't be
                            print "!!! OUTPUT MISMATCH ON INDEX %d !!!" % (work[id][0])
                    else:
                        # New completion
                
                        # Output the output (prefixed with hostname)
                        hostname = socket.gethostbyaddr(address[0])[0]
                        hostname = hostname.split(".")[0]
                        lines    = output.strip().split("\n")
                        for line in lines:
                            print "[%s] %s" % (hostname, line.strip())
            
                        work[id][1] = True
                        work[id][2] = output
                        done = done + 1
                        if done == len(work):
                            # We're all done, assuming there are no mismatches
                            print "Work completed"

                    sys.stdout.flush()
            except socket.timeout:
                # Check event and try again
                pass
    except:
        exc = sys.exc_info()
        print "exception in server thread: %s: %s" % (exc[0].__name__, exc[1])

def main(argv = None):
    if argv == None:
        argv = sys.argv
    
    # Get the path of the client
    client = os.path.dirname(os.path.abspath(argv[0])) + "/client.py"

    #
    # Parse options
    #
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

    #
    # Parse arguments
    #
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

    #
    # Compose arguments for client
    #
    args = ["-e", options.executable, "-c", options.config, "-p", options.program]

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

    # Add quotes around arguments with spaces
    args = map(lambda x: (x.find(" ") == -1 and x) or '"'+x+'"', args)

    #
    # Get idle hosts
    #
    hosts  = status.check_status(options.filename, options.executable)

    #
    # Start server
    #
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('',0)) 

    sockname = s.getsockname()
    sockname = [socket.getfqdn(), sockname[1]]
    print "Server listening on %s:%d" % (sockname[0], sockname[1])

    done = threading.Event()
    data = ThreadData(start, limit, step, done)
    t = threading.Thread(None, conn_thread, "conn_thread", (s,data))
    t.start()
    
    #        
    # Start clients
    #
    retval = 0
    pids   = []
    try:
        print "Starting clients..."
        for host in hosts:
            pid = start_client(host, client, args, sockname[0], sockname[1] )
            if not (pid is None):
                pids.append(pid)
            pass

        print "Started clients, waiting for completion..."
        while len(pids) > 0:
            os.waitpid(pids.pop(), 0)
    except:
        exc = sys.exc_info()
        print "%s: %s" % (exc[0].__name__, exc[1])
        retval = 1

    # Kill all pids
    for pid in pids:
        os.kill(pid, signal.SIGKILL)

    # Signal server thread to terminate and wait for it
    data.event.set()
    t.join()
    s.close()

    return retval

if __name__ == "__main__":
    sys.exit(main())
