#include <sys/mman.h>

#include "arena_allocator.hpp"

arena::arena(size_t arena_size) : _size(arena_size) {
    void *_ptr = mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (_ptr == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "cannot allocate registered buffer");
    if (madvise(_ptr, _size, MADV_DONTDUMP) < 0)
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) registered buffer");
    if (madvise(_ptr, _size, MADV_HUGEPAGE) < 0)
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_HUGEPAGE) registered buffer");
    if (mi_manage_os_memory_ex(_ptr, _size, true, true, true, -1, true, &_arena) != 0)
        throw std::system_error(ENOMEM, std::generic_category(), "cannot manage registered heap");
    _heap = unique_handle<mi_heap_t>(mi_heap_new_in_arena(_arena), mi_heap_destroy);
}
