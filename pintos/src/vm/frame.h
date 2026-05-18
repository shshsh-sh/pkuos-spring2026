#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/page.h"

struct frame
  {
    void *kpage;                  /**< Kernel virtual address of the frame. */
    struct spt_entry *spte;       /**< The supplemental page table entry associated with this frame. */
    struct list_elem elem;        /**< List element for the frame table. */
    bool pinned;                  /**< Whether the frame is pinned (i.e., cannot be evicted). */
  };

void frame_init (void);
struct frame *frame_alloc (enum palloc_flags flags);
void frame_free (struct frame *frame);

#endif