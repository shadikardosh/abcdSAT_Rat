/*
 * XAlloc.h
 *
 *  Created on: May 27, 2018
 *      Author: nadeem
 */

#ifndef XALLOC_H_
#define XALLOC_H_

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

namespace RGraph {

//=================================================================================================
// Simple layer on top of malloc/realloc to catch out-of-memory situtaions and provide some typing:

class OutOfMemoryException{};
static inline void* xrealloc(void *ptr, size_t size)
{
    void* mem = realloc(ptr, size);
    if (mem == NULL && errno == ENOMEM){
        throw OutOfMemoryException();
    }else {
        return mem;
	}
}

//=================================================================================================
}



#endif /* XALLOC_H_ */
