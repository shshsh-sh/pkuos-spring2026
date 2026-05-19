#include "vm/frame.h"
#include <list.h>
#include <stdlib.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

static struct list frame_table;
static struct lock frame_lock;
static struct list_elem *clock_hand;         // Pointer for the clock algorithm.

static void *do_eviction (void);

void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

struct frame *
frame_alloc (enum palloc_flags flags)
{
  lock_acquire (&frame_lock);

  struct frame *f = malloc (sizeof (struct frame));
  if (f == NULL)
    PANIC ("frame_alloc: out of memory");
    
  void *kpage = palloc_get_page (PAL_USER);
  if (kpage == NULL)
    {
      lock_release (&frame_lock);
      kpage = do_eviction ();
      lock_acquire (&frame_lock);
    }
  
  if (flags & PAL_ZERO)
    memset (kpage, 0, PGSIZE);

  f->kpage = kpage;
  f->spte = NULL;
  f->pinned = false;
  list_push_back (&frame_table, &f->elem);

  lock_release (&frame_lock);

  return f;
}

void
frame_free (struct frame *frame)
{
  lock_acquire (&frame_lock);
  list_remove (&frame->elem);
  lock_release (&frame_lock);

  palloc_free_page (frame->kpage);
  free (frame);
}

/*  */
static void *
do_eviction (void)
{
  ASSERT (!lock_held_by_current_thread (&frame_lock));

  lock_acquire (&frame_lock);

  /*
   * Implement the clock algorithm for eviction.
   * We maintain a "clock hand" that points to the next candidate frame for eviction.
   */
  while (true)
    {
      if (clock_hand == NULL || clock_hand == list_end (&frame_table))
        clock_hand = list_begin (&frame_table);
      struct frame *victim = list_entry (clock_hand, struct frame, elem);
      clock_hand = list_next (clock_hand);

      if (victim->pinned)
        continue;  // Skip pinned frames.
      if (victim->spte == NULL)
        continue;  // Skip frames that are not currently mapped to any page.

      bool accessed = pagedir_is_accessed (victim->spte->pagedir, victim->spte->upage);
      if (accessed)
        {
          pagedir_set_accessed (victim->spte->pagedir, victim->spte->upage, false);
          continue;  // Give it a second chance.
        }
      
      // Evict the victim frame.
      struct spt_entry *spte = victim->spte;
      void *kpage = victim->kpage;
      list_remove (&victim->elem);
      pagedir_clear_page (spte->pagedir, spte->upage);
      bool dirty = pagedir_is_dirty (spte->pagedir, spte->upage);
      spte->frame = NULL;
      free (victim);
      
      /*
       * If the page is dirty or was swapped out, we need to write it back to disk.
       * For file-backed pages, we can simply mark them as swapped and write to swap.
       * For already swapped pages, we just write to the same swap slot.
       */
      if (dirty || spte->type == PAGE_SWAP)
        {
          lock_release (&frame_lock);

          if (spte->type != PAGE_SWAP)
            {
              spte->swap_slot = swap_alloc ();
              spte->type = PAGE_SWAP;
            }
          else if (spte->swap_slot == 0)
            {
              spte->swap_slot = swap_alloc ();
            }
          swap_write (spte->swap_slot, kpage);

          lock_acquire (&frame_lock);
        }
      lock_release (&frame_lock);
      return kpage;
    }
}