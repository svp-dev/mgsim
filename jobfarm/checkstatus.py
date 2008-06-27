#!/usr/bin/python

import optparse, sys, status

def main(argv = None):
    if argv is None:
        argv = sys.argv

    # Parse options
    parser = optparse.OptionParser()
    parser.add_option("-f", "--file", dest="filename",
        help="read hosts from FILE", metavar="FILE", default="hosts")
    options, args = parser.parse_args()
    
    status.check_status(options.filename)
    return 0
    
if __name__ == '__main__':
    sys.exit(main())
