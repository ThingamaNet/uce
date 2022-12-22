#define NO_GLOBAL_ARENA_ALLOCATOR

struct MemoryArena {

	u8* data;
	u64 size = 0;
	u64 capacity = 0;
	String name = "unnamed";

	MemoryArena(u64 cap, String _name = "unnamed")
	{
		name = _name;
		capacity = cap;
		printf("(i) memory arena '%s' created with capacity of %llu bytes\n", name.c_str(), capacity);
		data = (u8*)malloc(cap);
	}

	~MemoryArena()
	{
		free(data);
	}

	void clear()
	{
		#ifdef DEBUG_MEMORY
		printf("(i) memory arena '%s' cleared after high mark of %llu bytes\n", name.c_str(), size);
		#endif
		size = 0;
	}

	void* get(u64 size_needed)
	{
		u64 size_aligned = 8 + (8 * ((size_needed) / 8));
		u8* result = data + size;
		if(size_aligned + size >= capacity)
		{
			printf("(!) memory arena '%s' capacity (%llu) exceeded %llu/%llu + %llu >= %llu\n",
				name.c_str(), capacity, size_needed, size_aligned, size, capacity);
			return(0);
		}
		size += size_aligned;
		#ifdef DEBUG_MEMORY_DETAILED
		printf("(i) memory arena '%s' [+%llu]:%p alloc %llu/%llu bytes\n", name.c_str(), size, result, size_needed, size_aligned);
		#endif
		return(result);
	}

};

MemoryArena* current_memory_arena = 0;

void switch_to_system_alloc()
{
#ifdef GLOBAL_ARENA_ALLOCATOR
	current_memory_arena = 0;
#endif
}

void switch_to_arena(MemoryArena* a)
{
#ifdef GLOBAL_ARENA_ALLOCATOR
	current_memory_arena = a;
#endif
}

#ifdef GLOBAL_ARENA_ALLOCATOR
void * operator new(decltype(sizeof(0)) n) noexcept(false)
{
	if(current_memory_arena)
	{
		return(current_memory_arena->get(n));
	}
	else
	{
		return(malloc(n));
	}
}

void operator delete(void * p) throw()
{
	if(current_memory_arena)
	{

	}
	else
	{
		free(p);
	}
}
#endif
