import optparse, sys, os, re, popen2

# Reads the complete file
def readfile(name):
    f = open(name,'r')
    lines = f.readlines()
    f.close()
    return lines
    
# Runs a command like the backtick operator
def run_command(args):
	p = popen2.Popen4(" ".join(args))
	p.tochild.close()
	output = p.fromchild.read()
	p.fromchild.close()
	return (p.wait(), output)

def check_status(filename, executable):
    # Read hosts file
    try:
        hosts = readfile(filename)
    except IOError, strerror:
        print strerror
        sys.exit(1)
    
    # Get the path of the load checker
    listusers = os.path.abspath(sys.path[0]) + "/listusers.py"

    # Get our username
    username = run_command(["whoami"])[1].split("\n")[0].strip()

    alive      = []
    idle       = []
    used       = []
    dead       = []
    running    = []
    obtruding  = []
    
    # Check all the hosts
    print "Checking %d hosts... " % len(hosts),
    sys.stdout.flush()
    
    for host in hosts:
        host = host.strip()

        # Check if host is alive
        if run_command(['ping','-q','-W','1','-c','1', host])[0] == 0:
            # Get list of users logged in at the host
            output = run_command(['rsh', host, listusers,executable])
            if output[0] == 0:
                result = output[1].split('\n')[0].strip().split(' ')
                try:
                    # Parse list and update statistics
                    user_present  = result[0] == 'True'   # Conservative
                    other_present = result[1] != 'False'  # Liberal
                    
                    # Host is alive
                    alive.append(host)
                    
                    if user_present:
                        if other_present:
                            obtruding.append(host)
                        running.append(host)
                    elif other_present:
                        used.append(host)
                    else:
                        idle.append(host)
                    host = ''
                except:
                    host = host # Do nothing

        if host != "":
            # Host is dead or not responding properly
            dead.append(host)

    # Report!
    print "%d hosts alive; %d hosts dead: %s" % (len(alive), len(dead), " ".join(dead))
    print "* %3d hosts running simulations; %d obtruding: %s" % (len(running), len(obtruding), " ".join(obtruding))
    print "* %3d hosts in use: %s" % (len(used), " ".join(used))
    print "* %3d hosts idle" % len(idle)
    
    return idle
