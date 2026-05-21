#include "vm/page.h"
#include <hash.h>
#include <stdlib.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

unsigned
spt_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spt_entry *page = hash_entry (e, struct spt_entry, hash_elem);
  return hash_bytes (&page->upage, sizeof page->upage);
}

bool
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct spt_entry *page_a = hash_entry (a, struct spt_entry, hash_elem);
  const struct spt_entry *page_b = hash_entry (b, struct spt_entry, hash_elem);
  return page_a->upage < page_b->upage;
}

struct spt_entry*
spt_lookup (struct hash *spt, const void *upage)
{
  struct spt_entry temp;
  temp.upage = pg_round_down (upage);
  struct hash_elem *e = hash_find (spt, &temp.hash_elem);
  return e != NULL ? hash_entry (e, struct spt_entry, hash_elem) : NULL;
}

bool
spt_insert_page (struct hash *spt, struct spt_entry *page)
{
  struct hash_elem *e = hash_insert (spt, &page->hash_elem);
  return e == NULL; 
}

void
spt_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *page = hash_entry (e, struct spt_entry, hash_elem);
  if (page->frame != NULL)
    {
      pagedir_clear_page (thread_current()->pagedir, page->upage);
      frame_free (page->frame);
    }

  if (page->type == PAGE_SWAP && page->swap_slot != 0)
    swap_free (page->swap_slot);
  free (page);
}

/* Loads the page containing UPAGE into memory. */
bool
vm_load_page (void *upage, bool pin)
{
  upage = pg_round_down (upage);
  struct thread *cur = thread_current ();
  struct spt_entry *spte = spt_lookup (&cur->process->spt, upage);
  if (spte == NULL)
    {
      // No page entry for this address. Check if it's a stack growth.
      if (upage >= (void *) PHYS_BASE - 0x800000 && upage < (void *) PHYS_BASE) // Stack growth.
        {
          if ((uintptr_t) upage < (uintptr_t) cur->user_esp - 32) // Not a valid stack access.
            return false;
          
          spte = malloc (sizeof (struct spt_entry));
          if (spte == NULL)
            return false;
          
          spte->upage = upage;
          spte->writable = true;
          spte->file = NULL;
          spte->file_offset = 0;
          spte->read_bytes = 0;
          spte->zero_bytes = PGSIZE;
          spte->swap_slot = 0;
          spte->type = PAGE_ZERO;
          spte->frame = NULL;
          spte->mapid = 0;
          spte->pagedir = cur->pagedir;

          spt_insert_page (&cur->process->spt, spte);
        }
      else
        return false;  // Not a valid page and not stack growth.
    }
  if (spte->frame != NULL)
    return true;  // Page is already loaded.
  
  enum palloc_flags flags = PAL_ZERO;
  struct frame *frame = frame_alloc (flags);
  if (frame == NULL)
    return false;  // No free frame available.
  
  switch (spte->type)
    {
      case PAGE_ZERO:
        break;
      case PAGE_FILE:
        file_seek (spte->file, spte->file_offset);
        if (file_read_at (spte->file, frame->kpage, spte->read_bytes, spte->file_offset) != (int) spte->read_bytes)
          {
            frame_free (frame);
            return false;  // Failed to read from file.
          }
        break;
      case PAGE_SWAP:
        swap_read (spte->swap_slot, frame->kpage);
        break;
    }
  
  if (!install_page (upage, frame->kpage, spte->writable))
    {
      frame_free (frame);
      return false;  // Failed to map page.
    }
  
  spte->frame = frame;
  frame->spte = spte;
  spte->pagedir = cur->pagedir;

  if (pin)
    frame->pinned = true;
  return true;
}

/*
 * Loads the pages in the specified range and pins them in memory.
 */
bool
vm_load_and_pin_range (void *addr, size_t size)
{
  if (size == 0)
    return true;  // Nothing to load or pin.
  
  struct thread *cur = thread_current ();
  void *start = pg_round_down (addr);
  void *end = pg_round_down (addr + size - 1);
  for (void *p = start; p <= end; p += PGSIZE)
    {
      if (!vm_load_page (p, true))
        return false;  // Failed to load page.
    }
  return true;
}

void
vm_unpin_range (void *addr, size_t size)
{
  if (size == 0)
    return;  // Nothing to unpin.
  
  struct thread *cur = thread_current ();
  void *start = pg_round_down (addr);
  void *end = pg_round_down (addr + size - 1);
  for (void *p = start; p <= end; p += PGSIZE)
    {
      struct spt_entry *spte = spt_lookup (&cur->process->spt, p);
      if (spte != NULL && spte->frame != NULL)
        spte->frame->pinned = false;
    }
}