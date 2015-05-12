# Introduction

cq (short for command queue) can be used to feed jobs from multiple hosts
(clients) into a single host (server). It was originally conceived for remotely
building packages on a build host. In turn the build host would provide the
requester with a completion notice so it may take some action (usually to
install the package). However, the final implementation is generic so it can be
used for more than just this scenario.

# Requirements

- ØMQ
- protobuf-c
- munge

# TODO

- logging
- daemonize server
- setuid/setgid before running command
