#!/usr/bin/python

import sys, os

# Runs a command like the backtick operator
def run_command(args):
    fd = os.popen(" ".join(args),"r")
    output = fd.read()
    return (fd.close(), output)

def main(argv = None):
    if argv is None:
        argv = sys.argv
    
    # Parse aruments
    try:
        executable = argv[1]
        if not os.path.isfile(executable):
            raise IndexError
        executable = os.path.basename(executable)
    except (IndexError):
        print "Expected the executable path as argument"
        return 1
    
    # Get our username
    username = run_command(["whoami"])[1].split("\n")[0].strip()

    is_self  = False
    is_other = False
    
    # Get list of users logged in at the host
    output = run_command(['who -q'])[1]
    users = output.split('\n')[0].strip().split(' ')
    for user in users:
        if user != '' and user != username:
            is_other = True
            break
            
    # Now see if we have the simulation running
    output = run_command(['ps','-u',username])[1].split("\n")
    for line in output:
        if line.find(executable) != -1:
            is_self = True    
            break
    
    print is_self,is_other
    return 0

if __name__ == '__main__':
    sys.exit(main())

