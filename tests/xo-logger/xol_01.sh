#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Exercises xo-logger's local message-construction path.  --test-mode
# fakes the timestamp/hostname/pid (via xo_set_unit_test_mode()) and
# captures the formatted message via xo_set_syslog_handler() instead
# of actually opening a socket to syslogd, so the output here is
# fully deterministic.  Networking options (-h/-4/-6/-A/-S/-P) are
# not exercised here; see ../../xo-logger/test-server.py for manual
# testing of remote delivery.

XO_LOGGER=$1
shift

${XO_LOGGER} --test-mode \
    'Disk {:disk} is at {:percent/%d}%\n' /dev/da0 97

${XO_LOGGER} --test-mode -p local0.error -t diskmon \
    'Disk {:disk} is at {:percent/%d}%\n' /dev/da0 97

${XO_LOGGER} --test-mode -p 131 -t diskmon \
    'Disk {:disk} is at {:percent/%d}%\n' /dev/da0 97

${XO_LOGGER} --test-mode -H otherhost.example.com \
    'Message with overridden hostname\n'

${XO_LOGGER} --test-mode -s -t print-test 'Also printed to stderr\n'

echo "line one
line two
line three" | ${XO_LOGGER} --test-mode -t stdin-test
