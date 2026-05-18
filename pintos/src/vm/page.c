#include "vm/page.h"
#include <hash.h>
#include <stdlib.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

unsigned
spt_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  const struct spt_entry *page = hash_entry (e, struct spt_entry, hash_elem);
  return hash_bytes (&page->upage, sizeof page->upage);
}

bool
spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct spt_entry *page_a = hash_entry (a, struct spt_entry, hash_elem);
  const struct spt_entry *page_b = hash_entry (b, struct spt_entry, hash_elem);
  return page_a->upage < page_b->upage;
}

struct spt_entry*
spt_lookup(struct hash *spt, const void *upage)
{
  struct spt_entry temp;
  temp.upage = pg_round_down (upage);
  struct hash_elem *e = hash_find (spt, &temp.hash_elem);
  return e != NULL ? hash_entry (e, struct spt_entry, hash_elem) : NULL;
}

bool
spt_insert_page(struct hash *spt, struct spt_entry *page)
{
  struct hash_elem *e = hash_insert (spt, &page->hash_elem);
  return e == NULL; 
}

void
spt_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *page = hash_entry (e, struct spt_entry, hash_elem);
  if (page->frame != NULL)
    {
      pagedir_clear_page (thread_current()->pagedir, page->upage);
      frame_free (page->frame);
    }

  if (page->type == PAGE_SWAP)
    {
      // TODO: Free swap slot.
    }
  free (page);
}