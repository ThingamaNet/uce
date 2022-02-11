# uce

## Current State

This is in the early stages of development. Don't use this for anything important (or at all)!

## Udo's C++ Entry Points

The aim of this project is to make a PHP-like runtime that enables server-side "scripting" using C/C++. At the core is a multi-worker FastCGI server that can be talked to from Nginx or similar front-end servers. UCE has a shared-nothing isolated architecture to serve page requests. To minimize the potential for memory leaks, UCE uses a per-request memory arena. UCE also provides a PHP-like API to ease web development. Advanced features such as an integrated WebSockets broker are planned. UCE aims to use minimal dependencies (at the moment, the only dependency is the Clang compiler).


