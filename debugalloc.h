#ifdef _WIN32
#include <stddef.h>
#else
#include <stdint.h>
#endif

struct lua_alloc_event
{
	intptr_t ptr, newptr;
	size_t siz;
	char type; // 'a' = alloc, 'f' = free, 'r' = resize
	lua_alloc_event() {}
	lua_alloc_event(char t, void* p, size_t s, void* np=NULL): 
		ptr((intptr_t)p), newptr((intptr_t)np), siz(s), type(t) {}
};


extern void lua_debug_alloc_enable(bool enable);
extern bool lua_debug_getnextalloc(lua_alloc_event *ptr);

