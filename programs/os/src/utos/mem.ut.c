#include <utos/switchboard.ut.h>
#include <utos/mem.ut.h>

thread void _mem_do_alloc(shared int, shared void*);
thread void _mem_do_free(shared int, shared void*);

/**** IMPLEMENTATION OF LIBRARY THREADS ******/

thread void utsys_mem_init() {

  UTSYS_SB_SEND(_SB_REGISTER_SERVICE | (_SB_MEM_ALLOC << 8), &_mem_do_alloc);

  UTSYS_SB_SEND(_SB_REGISTER_SERVICE | (_SB_MEM_FREE << 8), &_mem_do_free);
 
}

thread void utsys_mem_alloc(shared int size, shared void *p)
{
  int code = _SB_MEM_ALLOC | (size_ret1 << 8);
  void *ptr = 0;

  UTSYS_SB_EXCHANGE(code, ptr);
 
  p = ptr;
  size = code;
}

thread void utsys_mem_free(shared void *p)
{
  UTSYS_SB_SEND(_SB_MEM_FREE, p);
}


/****** IMPLEMENTATION OF MEMORY ALLOCATION ROUTINES *******/


/* this needs to be set by the linker, aligned. The
   data at that location must be zero. */
extern int __heap_base;


/* some utility globals */
unsigned *__free_list_base = 0;
void *__heap_ptr = &__heap_base;

/* layout in the free list:
   - word -3: type of previous block
   - word -2: pointer to the beginning of the previous block
   - word -1: size of this block (in words)
   - word 0: pointer to the next free block (zero if none)
   - word size-3: 1 (flag = free block)
   - word size-2: pointer to the beginning of this block
   - word size-1: size of the next block (in words)
*/
/* layout of an allocated block:
   - word -3: type of previous block
   - word -2: pointer to the beginning of the previous block
   - word -1: size of this block (in words)
   - word size-3: 0 (flag : busy block)
   - word size-2: pointer to the beginning of this block
   - word size-1: size of the next block  (in words)
*/


/* 
   _mem_do_alloc

   allocation rountine: this is called by
   the switchboard thread with guarantee that no
   other switchboard service is currently running
   (protects access to memory)
*/

thread void _mem_do_alloc(shared int code, shared void *ptr) {
  /* requested size in bytes */
  unsigned reqsz = code >> 8;
  /* requested size in words, adding 4 words for pointers */
  unsigned sz = (reqsz / sizeof(unsigned)) + 4
  unsigned *block = 0;

  /* first search in the free list if there is something */
  unsigned **free_chain_prevptr;
  unsigned *free_block;
  free_chain_prevptr = &__free_list_base;
  while(1) {
      free_block = *free_chain_prevptr;
      if (!free_block) 
	break; /* end of chain */

      /* a free block: is it big enough? */
      unsigned freeblock_size = *(free_block - 1);
      if (freeblock_size < sz)
	{
	  /* not big enough, continue searching */
	  free_chain_prevptr = (unsigned**)(void*)free_block;
	  continue;
	}
      /* the free block is big enough */

      /* should we split it ? */
      if (sz < (freeblock_size - 4)) {
	/* yes, split */
	
	block = free_block;
	*(block - 1) = sz;

	unsigned *split_block = block + sz;
	*(split_block) = *(unsigned**)(void*)free_block;
	*free_chain_prevptr = split_block; 
	*(split_block - 1) = (free_block + freeblock_size) - split_block;
	*(split_block - 2) = block;
	*(split_block - 3) = 0; /* busy */
	*(free_block + freeblock_size - 2) = split_block;
	*(free_block + freeblock_size - 3) = 1; /* free */

      } else {
	/* no, reuse entirely */

	/* set previous "next free block pointer" to the next free block */
	*(free_chain_prevptr) = *(unsigned**)(void*)free_block;
	*(free_block + freeblock_size - 3) = 0; /* busy */
      }
      
      break;
      
  }

  if (!block) {
    /* if no free block was found, make a new block */
    block = (unsigned*) __heap_ptr;
    (unsigned*)__heap_ptr += sz;
    *(block - 1) = sz;
    *(block + sz - 3) = 0;
    *(unsigned**)(void*)(block + sz - 2) = block;
    *(block + sz - 1) = 0; /* last block */
  }

  ptr = block;
  code = sz;
}

/* 
   _mem_do_free

   deallocation rountine: this is called by
   the switchboard thread with guarantee that no
   other switchboard service is currently running
   (protects access to memory)
*/

thread void _mem_do_free(shared int unused, shared void* ptr) {

  unsigned * block = (unsigned*) ptr;
  unsigned size_of_block = *(block-1);

  /* check the block is indeed busy */
  assert( *(block + size_of_block - 3) == 0 );

  /* look at the type of the previous block */
  unsigned type = *(block - 3);
  if (type == 1) /* free block */
    {
      /* then merge with previous */
      unsigned *prev = (unsigned*)(void*) *(block - 2);
      *(prev - 1) += size_of_block;
      *(unsigned**)(void*)(block + size_of_block - 2) = prev;

      /* adjust pointers */
      block = prev;
      size_of_block = *(block - 1);
    }

  /* look at the type of the next block */
  unsigned *next = block + size_of_block;
  unsigned size_of_next_block = *(next - 1);
  type = *(next + size_of_next_block - 3); 
  if (type == 1) /* also a free block */
    {
      /* coalesce */
      size_of_block = (*(block - 1) += size_of_next_block);
      *(unsigned**)(void*)(next + size_of_next_block - 2) = block;      
    }

  /* find first next free block */
  unsigned *first_free = block + size_of_block;
  while (1) {
    if ((unsigned)first_free >= (unsigned)__heap_ptr) {
      first_free = 0; 
      break; 
    }
    unsigned sz = *(first_free - 1);
    if (*(first_free + sz - 3) == 1)
      break;
    first_free += sz;
  }

  /* set next pointer */
  *(unsigned**)(void*)block = first_free;
  *(block + size_of_block - 3) = 1;  
}
