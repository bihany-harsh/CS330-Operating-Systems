#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
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
        *((void **)(ptr + SIZE_T_SIZE + SIZE_T_SIZE)) = NULL;   // prev

        ptr = ptr + SIZE_T_SIZE;
        free_head = ptr;
    }

    void *ptr = free_head;
    void *prev = NULL;

    // free block has metadata: size, prev, next
    // allocated block has metadata: size
	// size_t block_size = ALIGN(size + SIZE_T_SIZE);
    size_t block_size = size + SIZE_T_SIZE;
    if (block_size < FOURMB) {
        block_size = ALIGN(block_size);
    } else {
        size_t remainder = block_size % FOURMB;
        block_size = block_size + (FOURMB - remainder);
    }

    bool is_allocated = false;

    while(ptr != NULL) {
		size_t current_block_size = *((size_t*)(ptr - SIZE_T_SIZE));
        if (block_size <= current_block_size) {
            is_allocated = true;
            // split block
            if (*((unsigned long*)(ptr - ALIGNMENT)) - block_size < 24) { // if block_size - ptr->size < 24

                if (*((void**)(ptr)) != NULL) { // if ptr->next != NULL
                    void* next = *((void**)(ptr));
                    *((void**)(next + ALIGNMENT)) = *((void**)(ptr + ALIGNMENT));  // ptr->next->prev = ptr->prev
                }

                if (*((void**)(ptr + ALIGNMENT)) != NULL) { // if ptr->prev != NULL
                    void* prev = *((void**)(ptr + ALIGNMENT));
                    *((void**)(prev)) = *((void**)(ptr));  // ptr->prev->next = ptr->next
                }

                if (prev == NULL) {
                    free_head = *((void**)(ptr)); // free_head = ptr->next
                }

                return ptr;

            } else {
                // split block
                void* ptr2 = ptr + block_size;
                void* return_ptr = ptr;

                if (*((void**)(ptr)) != NULL) { // if ptr->next != NULL
                    *((void**)(ptr2)) = *((void**)(ptr));  // ptr2->next = ptr->next
                    *((void**)(*((void**)(ptr)) + ALIGNMENT)) = ptr2;  // ptr->next->prev = ptr2
                }

                if (*((void**)(ptr + ALIGNMENT)) != NULL) { // if ptr->prev != NULL
                    *((void**)(ptr2 + ALIGNMENT)) = *((void**)(ptr + ALIGNMENT));  // ptr2->prev = ptr->prev
                    *((void**)(*((void**)(ptr + ALIGNMENT)) + ALIGNMENT + ALIGNMENT)) = ptr2;  // ptr->prev->next = ptr2
                }

                *((unsigned long*)(ptr2 - ALIGNMENT)) = *((unsigned long*)(ptr - ALIGNMENT)) - block_size;  // ptr2->size = ptr->size - block_size
                *((unsigned long*)(return_ptr - ALIGNMENT)) = block_size; // ptr->size = block_size

                if (prev == NULL) {
                    free_head = ptr2;
                } else {
                    *((void **)(prev + SIZE_T_SIZE)) = ptr2;
                }

                return return_ptr;
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
        if (iter + *((size_t*)(iter - SIZE_T_SIZE)) == ptr) { // iter + iter's block_size = ptr
            NEXT = iter;
        }
        if (iter == ptr + *((size_t*)(ptr - SIZE_T_SIZE))) { // ptr + ptr's block_size = iter
            PREV = iter;
        }

        iter = *((void**)(iter));
    }

    if (NEXT != NULL){
        // merge ptr and NEXT
        *((size_t*)(ptr - SIZE_T_SIZE)) += *((size_t*)(NEXT - SIZE_T_SIZE));
                                        // ptr->size = ptr->size + NEXT->size

        // deleting the NEXT node from the free list
        if (*((void**)(NEXT)) != NULL) { // if NEXT->next != NULL
            void* NEXT_next = *((void**)(NEXT));
            *((void**)(NEXT_next + ALIGNMENT)) = *((void**)(NEXT + ALIGNMENT));  // NEXT->next->prev = NEXT->prev
        }
        if (*((void**)(NEXT + ALIGNMENT)) != NULL) { // if NEXT->prev != NULL
            void* NEXT_prev = *((void**)(NEXT + ALIGNMENT));
            *((void**)(NEXT_prev)) = *((void**)(NEXT));  // NEXT->prev->next = NEXT->next
        }
    }

    if (PREV != NULL) {
        // merge ptr and PREV 
        *((size_t*)(PREV - SIZE_T_SIZE)) += *((size_t*)(ptr - ALIGNMENT));
                                        // PREV->size = PREV->size + ptr->size

        // deleting the PREV node from the free list
        if (*((void**)(PREV)) != NULL) { // if PREV->next != NULL
            void* PREV_next = *((void**)(PREV));
            *((void**)(PREV_next + ALIGNMENT)) = *((void**)(PREV + ALIGNMENT));  // PREV->next->prev = PREV->prev
        }
        if (*((void**)(PREV + ALIGNMENT)) != NULL) { // if PREV->prev != NULL
            void* PREV_prev = *((void**)(PREV + ALIGNMENT));
            *((void**)(PREV_prev)) = *((void**)(PREV));  // PREV->prev->next = PREV->next
        }

        ptr = PREV;
    } else {
        // metadata
        *((void**)(ptr)) = NULL;  // ptr->next = NULL
        *((void**)(ptr + SIZE_T_SIZE)) = NULL;   // ptr->prev = NULL
    }

    // Add ptr to the head of the free list
    if (free_head != NULL) {
        *((void **)(free_head + SIZE_T_SIZE)) = ptr; // set prev of current head to ptr
        *((void**)(ptr)) = free_head; // set next of ptr to current head
    }
    free_head = ptr;

	return 0;
}	
