#!/usr/bin/python
import sys, socket, optparse, time, popen2, os, signal

def run_simulation(executable, index, overrides, simulation_args):
    index = str(index)

    # Construct the argument list
    args = ["nice", executable,  "-p", index]
    if overrides is not None:
        # Configuration overrides
        for o in overrides:
            args.extend(["-o", o + "=" + index])
    args.extend(simulation_args)

    p = None
    try:
        # Start process
        p = popen2.Popen4(" ".join(args))

        # Wait for it
        p.wait()
    except:
        # Something interrupted us, kill the process
        if p is not None:
            os.kill(p.pid, signal.SIGKILL)
        raise

    # Read, format and print output
    output = p.fromchild.read()
    p.fromchild.close()
    return output

def main(argv = None):
    if argv == None:
        argv = sys.argv
        
    #
    # Parse options
    #
    parser = optparse.OptionParser(usage="usage: %prog [options] server-name port")
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
        server = args[0]
        port   = int(args[1])
    except (IndexError, ValueError):
        print "Expected two arguments"
        return 1

    if options.config is None:
        print "Specify a configuration file"
        return 1

    if options.program is None:
        print "Specify a program file"
        return 1

    #
    # Compose arguments for simulation
    #
    args = ["-c", options.config]
    if options.register is not None:
        # Register override
        args.extend(["-" + options.register, index])

    if options.arguments is not None:
        # Arguments
        args.extend( options.arguments.split() )

    args.append(options.program)

    #
    # Connect to server
    #
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    hostname = socket.gethostname()
    address  = (server,port)
    
    try:
        command = ''
        while command != "KTHXBAI":
            conn = s.sendto("I CAN HAS DATA?", address)
            (data, _) = s.recvfrom(1024)

            # Seperate into command and data
            data    = data.split(":");
            command = data[0]
            data    = data[1:]
            
            if command == "YU MAEK":
                # There is work to be done
                id    = data[0]
                index = int(data[1])
                output = run_simulation(options.executable, index, options.overrides, args)
                s.sendto(":".join(["SRSLY",id,output]), address)
    except:
        exc = sys.exc_info()
        print "%s: %s: %s" % (hostname, exc[0].__name__, exc[1])
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
