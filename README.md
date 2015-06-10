# Lua Memory Profiler

This is a memory profiler I threw together for Lua 5.0, which is now obsolete.
I am uploading this code to github for posterity only.

It works like this:

First, I hooked `realloc()` and `free()` in `lmem.c` to point to my own
`debug_realloc` and `debug_free` functions, and I also created a Lua line hook.
My debug realloc and free functions push a note of the operation (it's a struct
that contains operation type, pointer, size, oldsize) into an internal queue,
which is read by the line hook function. So on each line, it keeps track of
what was allocated and what was freed. The realloc/free/line hook functions are
all written in C++ using STL containers (`deque`, `map`, `multimap`).

The first thing I tried was to get a stack dump inside the `realloc` and `free`
functions, but that just caused a huge mess; the line hook thing works better.
It still may not be terribly accurate but it does give you a general idea of
where your hotspots are.

Lua 5.1 supposedly has support for getting the actual memory consumption of a
particular table, which is really all I wanted in the first place.

Unfortunately I don't have any sample output handy. But it gives you two
profiles: currently allocated and total garbage. By default it stores
allocation/free counts per-line but you can change it to per-function by
removing the line hook and only using the call/return hooks.

-Andy <andy@a1k0n.net>
