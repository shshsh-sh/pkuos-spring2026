#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include "devices/block.h"

void swap_init (void);
block_sector_t swap_alloc (void);
void swap_free (block_sector_t slot);
void swap_write (block_sector_t slot, void *kpage);
void swap_read (block_sector_t slot, void *kpage);

#endif /* vm/swap.h */