#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#define FOURMB 4*1024*1024
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // header size

void *free_head = NULL;

void *memalloc(unsigned long size)
{
    printf("memalloc() called\n");

    if (size == 0) {
        return NULL;
    }

    if (free_head == NULL) {

        // if size is greater than 4MB, allocate a new block of size as the
        // smallest multiple of 4MB that is greater than size

        size_t block_size = size + SIZE_T_SIZE;
        if (block_size < FOURMB) {
            block_size = FOURMB;
        } else {
            size_t remainder = block_size % FOURMB;
            block_size = block_size + (FOURMB - remainder);
        }

        void *ptr = mmap(NULL, block_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return NULL;
        }

        // metadata
        *((size_t *)ptr) = block_size;
        *((void **)(ptr + SIZE_T_SIZE)) = NULL;  // next
        *((void **)(ptr + 2 * SIZE_T_SIZE)) = NULL;   // prev

        // ptr = ptr + SIZE_T_SIZE;
        free_head = ptr;
    }

    void *ptr = free_head;
    void *prev = NULL;

    // free block has metadata: size, prev, next
    // allocated block has metadata: size

    size_t block_size = size + SIZE_T_SIZE;
    if (block_size < FOURMB) {
        block_size = ALIGN(block_size);
    } else {
        size_t remainder = block_size % FOURMB;
        block_size = block_size + (FOURMB - remainder);
    }

    if (block_size < SIZE_T_SIZE * 3) {
        block_size = SIZE_T_SIZE * 3;
    }


    while(ptr != NULL) {
        // update: free_list points to beginning of blocks
        size_t current_block_size = *((size_t*)(ptr));

        if (block_size <= current_block_size) {
            // split block

            if (*((size_t*)(ptr)) - block_size < (SIZE_T_SIZE * 3)) {   // if block_size - ptr->size < 24
                // do not split block

                if (*((void**)(ptr + SIZE_T_SIZE)) != NULL) { // if ptr->next != NULL
                    void* next = *((void**)(ptr + SIZE_T_SIZE));
                    *((void**)(next + 2*SIZE_T_SIZE)) = *((void**)(ptr + 2*SIZE_T_SIZE));  // ptr->next->prev = ptr->prev
                }

                if (*((void**)(ptr + 2*SIZE_T_SIZE)) != NULL) { // if ptr->prev != NULL
                    void* prev = *((void**)(ptr + 2*SIZE_T_SIZE));
                    *((void**)(prev + SIZE_T_SIZE)) = *((void**)(ptr + SIZE_T_SIZE));  // ptr->prev->next = ptr->next
                }

                if (prev == NULL) { // if ptr == free_head
                    free_head = *((void**)(ptr + SIZE_T_SIZE)); // free_head = ptr->next
                }

                return ptr + SIZE_T_SIZE; // the returned allocated block should be ahead the size field

            } else {
                // split block
                void* ptr2 = ptr + block_size;

                if (*((void**)(ptr + SIZE_T_SIZE)) != NULL) { // if ptr->next != NULL
                    *((void**)(ptr2 + SIZE_T_SIZE)) = *((void**)(ptr + SIZE_T_SIZE));  // ptr2->next = ptr->next
                    *((void**)(*((void**)(ptr + SIZE_T_SIZE)) + 2*SIZE_T_SIZE)) = ptr2;  // ptr->next->prev = ptr2
                }

                if (*((void**)(ptr + 2*SIZE_T_SIZE)) != NULL) { // if ptr->prev != NULL
                    *((void**)(ptr2 + 2*SIZE_T_SIZE)) = *((void**)(ptr + 2*SIZE_T_SIZE));  // ptr2->prev = ptr->prev
                    *((void**)(*((void**)(ptr + 2*SIZE_T_SIZE)) + SIZE_T_SIZE)) = ptr2;  // ptr->prev->next = ptr2
                }

                *((size_t*)(ptr2)) = *((size_t*)(ptr)) - block_size;  // ptr2->size = ptr->size - block_size
                *((size_t*)(ptr)) = block_size; // ptr->size = block_size

                if (prev == NULL) { // if ptr == free_head
                    free_head = ptr2;
                } else {
                    *((void**)(prev + SIZE_T_SIZE)) = ptr2; // ptr->prev->next = ptr2
                }

                return ptr + SIZE_T_SIZE; // the returned allocated block should be ahead the size field
            }
        } else {
            prev = ptr;
            ptr = *((void **)(ptr + SIZE_T_SIZE));
        }
    }

    return NULL;
}

int memfree(void *ptr)
{
	printf("memfree() called\n");

    if (ptr == NULL) {
        return -1;
    }
    
    void *NEXT = NULL;
    void *PREV = NULL;

    void *iter = free_head;

    while(iter != NULL) {
        if (iter + *((size_t*)(iter)) + SIZE_T_SIZE == ptr) { // iter + iter's block_size = ptr
            NEXT = iter;
        }
        if (iter == ptr + *((size_t*)(ptr)) - SIZE_T_SIZE) { // ptr + ptr's block_size = iter
            PREV = iter;
        }

        iter = *((void**)(iter + SIZE_T_SIZE));     // iter = iter->next
    }

    if (NEXT != NULL){
        // merge ptr and NEXT
        *((size_t*)(ptr - SIZE_T_SIZE)) += *((size_t*)(NEXT));    // ptr->size = ptr->size + NEXT->size

        // deleting the NEXT node from the free list
        if (*((void**)(NEXT + SIZE_T_SIZE)) != NULL) { // if NEXT->next != NULL
            void* NEXT_next = *((void**)(NEXT + SIZE_T_SIZE));
            *((void**)(NEXT_next + 2*SIZE_T_SIZE)) = *((void**)(NEXT + 2*SIZE_T_SIZE));  // NEXT->next->prev = NEXT->prev
        }
        if (*((void**)(NEXT + 2*SIZE_T_SIZE)) != NULL) { // if NEXT->prev != NULL
            void* NEXT_prev = *((void**)(NEXT + 2*SIZE_T_SIZE));
            *((void**)(NEXT_prev + SIZE_T_SIZE)) = *((void**)(NEXT + SIZE_T_SIZE));  // NEXT->prev->next = NEXT->next
        }
    }

    if (PREV != NULL) {
        // merge ptr and PREV 
        *((size_t*)(PREV)) += *((size_t*)(ptr - SIZE_T_SIZE));    // PREV->size = PREV->size + ptr->size

        // deleting the PREV node from the free list
        if (*((void**)(PREV + SIZE_T_SIZE)) != NULL) { // if PREV->next != NULL
            void* PREV_next = *((void**)(PREV + SIZE_T_SIZE));
            *((void**)(PREV_next + 2*SIZE_T_SIZE)) = *((void**)(PREV + 2*SIZE_T_SIZE));  // PREV->next->prev = PREV->prev
        }
        if (*((void**)(PREV + 2*SIZE_T_SIZE)) != NULL) { // if PREV->prev != NULL
            void* PREV_prev = *((void**)(PREV + 2*SIZE_T_SIZE));
            *((void**)(PREV_prev + SIZE_T_SIZE)) = *((void**)(PREV + SIZE_T_SIZE));  // PREV->prev->next = PREV->next
        }

        ptr = PREV;
    } else {
        // metadata
        ptr = ptr - SIZE_T_SIZE;    // ptr init at the begin of block
        *((void**)(ptr + SIZE_T_SIZE)) = NULL;  // ptr->next = NULL
        *((void**)(ptr + 2*SIZE_T_SIZE)) = NULL;   // ptr->prev = NULL
    }

    // Add ptr to the head of the free list
    if (free_head != NULL) {
        *((void **)(free_head + 2*SIZE_T_SIZE)) = ptr; // set prev of current head to ptr
        *((void**)(ptr + SIZE_T_SIZE)) = free_head; // set next of ptr to current head
    }
    free_head = ptr;

	return 0;
}	
