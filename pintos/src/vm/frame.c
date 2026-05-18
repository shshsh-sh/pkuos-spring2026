#include "vm/frame.h"
#include <list.h>
#include <stdlib.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"

static struct list frame_table;
static struct lock frame_lock;

void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

struct frame *
frame_alloc (enum palloc_flags flags)
{
  void *kpage = palloc_get_page (PAL_USER | flags);
  if (kpage == NULL)
    PANIC ("frame_alloc: out of memory");

  struct frame *f = malloc (sizeof (struct frame));
  if (f == NULL)
    {
      palloc_free_page (kpage);
      PANIC ("frame_alloc: out of memory");
      return NULL;
    }

  f->kpage = kpage;
  f->spte = NULL;
  f->pinned = false;

  lock_acquire (&frame_lock);
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