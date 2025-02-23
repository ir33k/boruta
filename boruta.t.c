#include "boruta.c"
#include "walter.h"

TEST("storage")
{
	// struct storage s = {0};
	// char str[8];
	// size_t capacity;
	// 
	// str[0] = storage_push(&s, "first");
	// str[1] = storage_push(&s, "second");
	// str[2] = storage_push(&s, "third");
	// str[3] = storage_push(&s, "last");
	// 
	// SAME(str[0], "first", -1);
	// SAME(str[1], "second", -1);
	// SAME(str[2], "third", -1);
	// SAME(str[3], "last", -1);
	// 
	// capacity = s.cap;
	// storage_clear(&s);
	// OK(buf != 0);
	// OK(capacity == s.cap);
	// OK(sz == 0);
	// 
	// storage_free(&s);
	// OK(s.buf == 0);
	// OK(s.cap == 0);
	// OK(s.sz == 0);
}
