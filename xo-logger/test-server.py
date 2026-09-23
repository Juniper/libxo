#!/usr/bin/env python3
#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# A minimal UDP syslog server, for manually testing "xo-logger"'s
# remote delivery options (-h/-4/-6/-A/-S/-P).  Not run as part of
# "make check"; network delivery is not exercised by the automated
# test suite.  Start it, then point xo-logger at it, e.g.:
#
#   ./test-server.py --port 1514
#   xo-logger -h 127.0.0.1 -P 1514 'Disk {:disk} is at {:percent/%d}%\n' \
#       /dev/da0 97
#

import argparse
import datetime
import socket
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Minimal UDP syslog server for testing xo-logger")
    parser.add_argument("--host", default="0.0.0.0",
                         help="address to listen on (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=1514,
                         help="UDP port to listen on (default: 1514)")
    parser.add_argument("--inet6", action="store_true",
                         help="listen on an IPv6 socket instead of IPv4")
    args = parser.parse_args()

    family = socket.AF_INET6 if args.inet6 else socket.AF_INET
    sock = socket.socket(family, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))

    print(f"test-server.py: listening for syslog messages on "
          f"{args.host}:{args.port}/udp", file=sys.stderr)

    while True:
        data, addr = sock.recvfrom(65536)
        now = datetime.datetime.now().isoformat(timespec="seconds")
        message = data.decode("utf-8", errors="replace")
        print(f"[{now}] {addr[0]}:{addr[1]}: {message}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
