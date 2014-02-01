HttpPipe
========

Pipe an input to HTTP port

Usage: pipe [options]
Pipe standard input to a specific network.

Options:
  -V             Enable verbose output
  -h             Print this help and exit
  -v             Print program version and exit
  -d DEST        Pipe destination URL
  -c LEVEL       Enable ZIP compress (1~9)
  -s BUFSIZ      The buffer size, default 2 MB
  -r RATE        Transfer rate, default 100 K/s
  -n TRY         Failed connect try, default 3 times
  -i INTERVAL    Transfer interval in idle, default 5 minutes
  -l LIMIT       Limit to transfer occur in idle, default 3 times
