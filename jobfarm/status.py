import optparse, sys, os, re

# Reads the complete file
def readfile(name):
    f = open(name,'r')
    lines = f.readlines()
    f.close()
    return lines
    
# Runs a command like the backtick operator
def run_command(args):
    fd = os.popen(" ".join(args),"r");
    output = fd.read()
    return (fd.close(), output)

def check_status(filename):
    # Read hosts file
    try:
        hosts = readfile(filename)
    except IOError, strerror:
        print strerror
        sys.exit(1)
    
    # Get our username
    username = run_command("whoami")[1].split("\n")[0].strip()

    num_idle      = 0
    num_used      = 0
    num_running   = 0
    num_obtruding = 0
    num_alive     = 0
    idle_hosts    = []
    
    # Check all the hosts
    print "Checking %d hosts... " % len(hosts),
    sys.stdout.flush()
    
    for host in hosts:
        host = host.strip()

        # Check if host is alive
        status = run_command(['ping','-q','-W','1','-c','1', host])[0];
        
        if status == None:
            # Host is alive
            num_alive += 1
            
            # Get list of users logged in at the host
            output = run_command(['rsh', host, 'who -q'])[1]
            users = output.split('\n')[0].split(' ')

            # Parse list and update statistics
            user_present  = False
            other_present = False
            for user in users:
                if user == username:
                    user_present = True
                elif user != '':
                    other_present = True
 
            if user_present:
                if other_present:
                    num_obtruding += 1
                num_running += 1
            elif other_present:
                num_used += 1
            else:
                num_idle += 1
                idle_hosts.append(host)

    # Report!
    print "%d hosts alive, of which" % num_alive
    print "* %3d hosts running simulations (%d obtruding)" % (num_running, num_obtruding)
    print "* %3d hosts in use" % num_used
    print "* %3d hosts idle" % num_idle
    
    return idle_hosts
