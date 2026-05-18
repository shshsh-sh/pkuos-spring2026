#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include "threads/synch.h"
#include "threads/vaddr.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)  /* 8 */

static struct block *swap_device;          /* Swap device. */
static struct bitmap *swap_bitmap;         /* Bitmap to track swap slots. */
static struct lock swap_lock;              /* Lock to protect swap operations. */

/* Initialize the swap system. */
void
swap_init (void) 
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("No swap device found");

  size_t swap_size = block_size (swap_device);
  size_t num_slots = swap_size / SECTORS_PER_PAGE;
  swap_bitmap = bitmap_create (num_slots);
  if (swap_bitmap == NULL)
    PANIC ("Failed to create swap bitmap");

  lock_init (&swap_lock);
}

/* Allocate a swap slot. */
block_sector_t
swap_alloc (void) 
{
  lock_acquire (&swap_lock);
  size_t slot = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  lock_release (&swap_lock);
  if (slot == BITMAP_ERROR)
    PANIC ("No free swap slots available");
  return slot * SECTORS_PER_PAGE;  /* Return the starting sector of the allocated slot. */
}

/* Free a swap slot. */
void
swap_free (block_sector_t slot) 
{
  lock_acquire (&swap_lock);
  ASSERT (slot % SECTORS_PER_PAGE == 0);  /* Ensure slot is page-aligned. */
  bitmap_set (swap_bitmap, slot / SECTORS_PER_PAGE, false);
  lock_release (&swap_lock);
}

/* Write a page to the swap slot. */
void
swap_write (block_sector_t slot, void *kpage) 
{
  ASSERT (slot % SECTORS_PER_PAGE == 0);  /* Ensure slot is page-aligned. */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) 
    block_write (swap_device, slot + i, (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);
}

/* Read a page from the swap slot. */
void
swap_read (block_sector_t slot, void *kpage) 
{
  ASSERT (slot % SECTORS_PER_PAGE == 0);  /* Ensure slot is page-aligned. */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) 
    block_read (swap_device, slot + i, (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);
}