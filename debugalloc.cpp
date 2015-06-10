#include <deque>
#include "debugalloc.h"

static std::deque<lua_alloc_event> _alloc_q;
static bool _alloc_q_enabled=false;

void lua_debug_alloc_enable(bool e) { _alloc_q_enabled = e; }

bool lua_debug_getnextalloc(lua_alloc_event *ptr)
{
	if(_alloc_q.empty()) return false;
	*ptr = _alloc_q.front();
	_alloc_q.pop_front();
	return true;
}

extern "C" void* lua_debug_realloc(void* b, size_t os, size_t s)
{
	void* newblock = realloc(b,s);
	if(_alloc_q_enabled) {
		if(b) {
			_alloc_q.push_back(lua_alloc_event('r', b, s, newblock));
//			_alloc_q.push_back(lua_alloc_event('f', b, os));
//			_alloc_q.push_back(lua_alloc_event('a', newblock, s));
		} else {
			_alloc_q.push_back(lua_alloc_event('a', newblock, s));
		}
	}
	return newblock;
}

extern "C" void lua_debug_free(void* b, size_t os)
{
	if(b && _alloc_q_enabled) 
		_alloc_q.push_back(lua_alloc_event('f', b, os));
	free(b);
}

