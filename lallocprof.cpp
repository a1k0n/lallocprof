#include <stdlib.h>
#include <string>
#include <map>
#include <assert.h>
#include "debugalloc.h"
extern "C" {
#include <lua/lua.h>
}

#ifdef _WIN32
#define snprintf _snprintf
#endif

typedef std::string alloccontext;
struct allocinfo
{
	size_t size;
	alloccontext ctx;
	allocinfo() {}
	allocinfo(size_t s, alloccontext c): size(s),ctx(c) {}
};
typedef std::map<intptr_t, allocinfo>::iterator allocmapitr;
static std::map<intptr_t, allocinfo> _allocmap;
typedef std::map<alloccontext, size_t>::iterator garbageitr;
static std::map<alloccontext, size_t> _totalgarbage;

static alloccontext _current_fn="(init)";

static void addgarbage(const alloccontext &ctx, size_t siz)
{
	garbageitr g = _totalgarbage.find(ctx);
	if(g == _totalgarbage.end())
		_totalgarbage[ctx]=siz;
	else
		_totalgarbage[ctx]+=siz;
}

static void lua_alloc_hook(lua_State *L, lua_Debug *ar)
{
	lua_alloc_event aev;
	while(lua_debug_getnextalloc(&aev)) {
		//printf("type %c siz %d ptr %08x context %s\n", aev.type, aev.siz, 
		//		aev.ptr, _current_fn.c_str());
		if(aev.type == 'a') {
			// allocation; attribute it to _current_fn
			assert(_allocmap.find(aev.ptr)==_allocmap.end());
			_allocmap[aev.ptr] = allocinfo(aev.siz, _current_fn);
		} else if(aev.type == 'r') {
			// realloc; change size and location of existing block without
			// changing its originating context
			allocmapitr i = _allocmap.find(aev.ptr);
			if(i!=_allocmap.end()) {
				unsigned oldsiz = (*i).second.size;
				if(oldsiz == aev.siz && aev.ptr == aev.newptr) {
					printf("useless realloc of %d bytes in %s\n",
							oldsiz, _current_fn.c_str());
				} else if(oldsiz < aev.siz) {
					// increasing size, so count it as a free in the old
					// context ...
					addgarbage((*i).second.ctx, oldsiz);
					_allocmap.erase(i);
					// and an allocation in the current context
					_allocmap[aev.newptr] = allocinfo(aev.siz, _current_fn);
				} else {
					// decreasing size, so count it as a garbage collect
					// from the previous context
					//printf("shrinking %p->%p from %d->%d oldctx %s newctx %s\n", 
					//		aev.ptr, aev.newptr, oldsiz, aev.siz,
					//		(*i).second.ctx.c_str(), _current_fn.c_str());
					if(aev.ptr == aev.newptr) {
						// in-place resize?  easy enough
						(*i).second.size = aev.siz;
					} else {
						// attribute the new pointer to the old context
						assert(_allocmap.find(aev.newptr)==_allocmap.end());
						alloccontext fn = (*i).second.ctx;
						_allocmap[aev.newptr] = allocinfo(aev.siz, fn);
						_allocmap.erase(i);
					}
					// add the size reduction amount as garbage
					addgarbage((*i).second.ctx, oldsiz-aev.siz);
				}
			} else {
				_allocmap[aev.newptr] = allocinfo(aev.siz, "(init);\n         "+
						_current_fn);
			}
		} else {
			// free; release it
			allocmapitr i = _allocmap.find(aev.ptr);
			if(i!=_allocmap.end()) {
				addgarbage((*i).second.ctx, (*i).second.size);
				_allocmap.erase(i);
			}
		}
	}
	lua_Debug dbg;
	// ar->event denotes which event
	// but whether it's a line or a call, i'm gonna do the same thing.
	if(ar->event == LUA_HOOKRET) {
		// returning from a function; our new context is its parent.
		if(lua_getstack(L, 1, &dbg))
			ar = &dbg;
		else {
			_current_fn = "(unknown C caller of "+_current_fn+")";
			return;
		}
	} else if(ar->event == LUA_HOOKTAILRET) {
		// umm.. returning from a tail call, so leave our context alone
		return;
	}

	lua_getinfo(L, "nSl", ar);
	char str[1024]; 
	if(strcmp(ar->short_src, "[C]")) {
		snprintf(str, 1024, "%s [%s:%d]",
				ar->name ? ar->name : "?", ar->short_src, ar->currentline);
	} else {
		lua_Debug dbg;
		if(lua_getstack(L, 1, &dbg)) {
			lua_getinfo(L, "nSl", &dbg);
			snprintf(str, 1024, "%s called by %s [%s:%d]",
				ar->name ? ar->name : "?", 
				dbg.name ? dbg.name : "?", 
				dbg.short_src, dbg.currentline);
		} else {
			snprintf(str, 1024, "%s (unknown C caller)",
				ar->name ? ar->name : "?");
		}
	}
	_current_fn = str;
	//printf("context: %s\n", _current_fn.c_str());
}

int LuaStartAllocProfiler(lua_State *L)
{
	lua_debug_alloc_enable(true);
	lua_sethook(L, lua_alloc_hook, LUA_MASKLINE|LUA_MASKCALL|LUA_MASKRET, 0);
//	lua_sethook(L, lua_alloc_hook, LUA_MASKCALL|LUA_MASKRET, 0);
	return 0;
}

struct revsort
{
	bool operator()(size_t a, size_t b) const {
		return b<a;
	}
};

void LuaDumpAllocProfile(FILE *fp)
{
	// first run through each allocated block and tally up the size
	// per-context
	// then sort the result biggest to smallest
	{
		allocmapitr i;
		unsigned long tally=0;
		std::map<alloccontext, size_t> sizetally;
		std::map<alloccontext, size_t> numtally;
		for(i=_allocmap.begin();i!=_allocmap.end();i++) {
			if(sizetally.find((*i).second.ctx) == sizetally.end()) {
				sizetally[(*i).second.ctx] = (*i).second.size;
				numtally[(*i).second.ctx] = 1;
			} else {
				sizetally[(*i).second.ctx] += (*i).second.size;
				numtally[(*i).second.ctx] ++;
			}
		}
		std::map<alloccontext, size_t>::iterator sitr;
		std::multimap<size_t, alloccontext, revsort> ctxsort;
		for(sitr=sizetally.begin();sitr!=sizetally.end();sitr++) {
			char newname[512];
			snprintf(newname, 512, "%s [%d blocks]", (*sitr).first.c_str(),
					numtally[(*sitr).first]);
			ctxsort.insert(std::pair<size_t, alloccontext>(
						(*sitr).second, alloccontext(newname)));
			tally += (*sitr).second;
		}

		fprintf(fp, "====== currently allocated: %lu bytes ======\n", tally);
		std::multimap<size_t, alloccontext, revsort>::iterator citr;
		for(citr=ctxsort.begin();citr!=ctxsort.end();citr++)
			fprintf(fp, "%8d %s\n", (*citr).first, (*citr).second.c_str());
	}
	{
		fprintf(fp, "\n====== garbage generated ======\n");
		std::map<alloccontext, size_t>::iterator sitr;
		std::multimap<size_t, alloccontext, revsort> ctxsort;
		for(sitr=_totalgarbage.begin();sitr!=_totalgarbage.end();sitr++)
			ctxsort.insert(std::pair<size_t, alloccontext>((*sitr).second, (*sitr).first));

		std::multimap<size_t, alloccontext, revsort>::iterator citr;
		for(citr=ctxsort.begin();citr!=ctxsort.end();citr++)
			fprintf(fp, "%8d %s\n", (*citr).first, (*citr).second.c_str());
	}
}

