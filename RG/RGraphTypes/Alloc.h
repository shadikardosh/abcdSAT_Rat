/*
 * Alloc.h
 *
 *  Created on: May 27, 2018
 *      Author: nadeem
 */

#ifndef ALLOC_H_
#define ALLOC_H_

#include "XAlloc.h"
#include "Vec.h"

namespace RGraph {

//=================================================================================================
// Simple Region-based memory allocator:

//=================================================================================================
// Simple Region-based memory allocator:

template<class T>
class RegionAllocator
{
    T*        memory;
    uint32_t  sz;
    uint32_t  cap;
    uint32_t  wasted_;

    void capacity(uint32_t min_cap);

 public:
    // TODO: make this a class for better type-checking?
    typedef uint32_t Ref;
    enum { Ref_Undef = UINT32_MAX };
    enum { Unit_Size = sizeof(uint32_t) };

    explicit RegionAllocator(uint32_t start_cap = 1024*1024) : memory(NULL), sz(0), cap(0), wasted_(0){ capacity(start_cap); }
    ~RegionAllocator()
    {
        if (memory != NULL)
            ::free(memory);
    }


    uint32_t size      () const      { return sz; }
	void SetSize(uint32_t newSize) {  sz = newSize; }
    void ClearWasted   ()            { wasted_ = 0;  }
    uint32_t wasted    () const      { return wasted_; }

    Ref      alloc     (int size);
    void     free      (int size)    { wasted_ += size; }

    // Deref, Load Effective Address (LEA), Inverse of LEA (AEL):
    T&       operator[](Ref r)       { assert(r >= 0 && r < sz); return memory[r]; }
    const T& operator[](Ref r) const { assert(r >= 0 && r < sz); return memory[r]; }

    T*       lea       (Ref r)       { assert(r >= 0 && r < sz); return &memory[ r]; }
    const T* lea       (Ref r) const { assert(r >= 0 && r < sz); return &memory[r]; }
    Ref      ael       (const T* t)  { assert((void*)t >= (void*)&memory[0] && (void*)t < (void*)&memory[sz-1]);
        return  (Ref)(t - &memory[0]); }

    void     moveTo(RegionAllocator& to) {
        if (to.memory != NULL) ::free(to.memory);
        to.memory = memory;
        to.sz = sz;
        to.cap = cap;
        to.wasted_ = wasted_;

        memory = NULL;

        sz = cap = wasted_ = 0;
    }
};

template<class T>
void RegionAllocator<T>::capacity(uint32_t min_cap)
{
    if (cap >= min_cap) return;

    uint32_t prev_cap = cap;
    while (cap < min_cap){
        // NOTE: Multiply by a factor (13/8) without causing overflow, then add 2 and make the
        // result even by clearing the least significant bit. The resulting sequence of capacities
        // is carefully chosen to hit a maximum capacity that is close to the '2^32-1' limit when
        // using 'uint32_t' as indices so that as much as possible of this space can be used.
        uint32_t delta = ((cap >> 1) + (cap >> 3) + 2) & ~1;
        cap += delta;

		if (cap <= prev_cap)
			throw OutOfMemoryException();

    }
     //printf(" .. (%p) cap = %u\n", this, cap);

    assert(cap > 0);
    memory = (T*)xrealloc(memory, sizeof(T)*cap);
}


template<class T>
typename RegionAllocator<T>::Ref
RegionAllocator<T>::alloc(int size)
{
    //printf("ALLOC called (this = %p, size = %d)\n", this, size); fflush(stdout);
    assert(size > 0);


	capacity(sz + size);

    uint32_t prev_sz = sz;
    sz += size;


    // Handle overflow:
	if (sz < prev_sz)

		throw OutOfMemoryException();


    return prev_sz;
}
}


#endif /* ALLOC_H_ */