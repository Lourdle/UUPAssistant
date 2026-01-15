#include <Windows.h>
#include <malloc.h>

inline
static void* alloc(size_t size)
{
	return HeapAlloc(reinterpret_cast<HANDLE>(_get_heap_handle()), 0, size);
}

inline
static void mfree(void* p)
{
	if (p)
		HeapFree(reinterpret_cast<HANDLE>(_get_heap_handle()), 0, p);
}

void* operator new(size_t size)
{
	return alloc(size);
}

void operator delete(void* p)
{
	mfree(p);
}

void* operator new[](size_t size)
{
	return alloc(size);
}

void operator delete[](void* p)
{
	mfree(p);
}
