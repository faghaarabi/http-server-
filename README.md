# COMP 4981 Assignment 3 - HTTP Server

## Overview
This project implements a multi-process HTTP 1.0 server in C using POSIX sockets.  
It supports GET, HEAD, and POST requests, serves files from a designated directory, stores POST data in an ndbm database, and dynamically reloads HTTP handling logic through a shared library.

## Features
- Pre-forked worker processes
- Concurrent client handling
- HTTP GET, HEAD, and POST support
- Static file serving from `www/`
- POST data storage in ndbm database
- Shared library loaded with `dlopen()`
- Automatic library reload when updated
- Separate database reader program

## Build
```bash
makes