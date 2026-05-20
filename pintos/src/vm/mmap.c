#include "vm/mmap.h"
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

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1) / (y))

static struct mmap_entry *mmap_lookup (struct list *mmap_list, mapid_t mapid);

mapid_t
mmap_map (int fd, void *addr)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;

  if (fd < 2 || fd >= MAX_FD_COUNT || proc->fd_table[fd] == NULL)
    return MAP_FAILED;

  if (addr == NULL || !is_user_vaddr (addr) || pg_ofs (addr) != 0)
    return MAP_FAILED;

  if (addr < (void *) PGSIZE || addr >= (void *) PHYS_BASE - 0x800000) // Not a valid user address or too close to the kernel space.
    return MAP_FAILED;

  struct file *file = proc->fd_table[fd];
  size_t file_size = file_length (file);
  if (file_size == 0)
    return MAP_FAILED;
  
  size_t page_count = DIV_ROUND_UP (file_size, PGSIZE);
  for (size_t i = 0; i < page_count; i++)
    {
      void *upage = addr + i * PGSIZE;
      if (!is_user_vaddr (upage))
        return MAP_FAILED;
      if (spt_lookup (&proc->spt, upage) != NULL)
        return MAP_FAILED;
    }
  
  struct mmap_entry *entry = malloc (sizeof (struct mmap_entry));
  if (entry == NULL)
    return MAP_FAILED;
  
  struct file *mmap_file = file_reopen (file);
  if (mmap_file == NULL)
    {
      free (entry);
      return MAP_FAILED;
    }
  
  entry->mapid = proc->next_mapid++;
  entry->file = mmap_file;
  entry->addr = addr;
  entry->size = file_size;

  for (size_t i = 0; i < page_count; i++)
    {
      struct spt_entry *spte = malloc (sizeof (struct spt_entry));
      if (spte == NULL)
        {
          for (size_t j = 0; j < i; j++)
            {
              void *upage = addr + j * PGSIZE;
              struct spt_entry temp;
              temp.upage = upage;
              struct hash_elem *e = hash_delete (&proc->spt, &temp.hash_elem); 
              if (e != NULL)
                free (hash_entry (e, struct spt_entry, hash_elem));
              }
          file_close (mmap_file);
          free (entry);
          return MAP_FAILED;
        }
      
      size_t offset = i * PGSIZE;
      size_t read_bytes = (offset + PGSIZE <= file_size) ? PGSIZE : (file_size - offset);

      spte->upage = addr + offset;
      spte->type = PAGE_FILE;
      spte->writable = true;
      spte->file = mmap_file;
      spte->file_offset = offset;
      spte->read_bytes = read_bytes;
      spte->zero_bytes = PGSIZE - read_bytes;
      spte->swap_slot = 0;
      spte->frame = NULL;
      spte->pagedir = cur->pagedir;
      spte->mapid = entry->mapid;

      spt_insert_page (&proc->spt, spte);
    }
  list_push_back (&proc->mmap_list, &entry->list_elem);
  return entry->mapid;
}

void
mmap_unmap (mapid_t mapid)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;
  struct mmap_entry *entry = mmap_lookup (&proc->mmap_list, mapid);
  if (entry == NULL)
    return;

  size_t page_count = DIV_ROUND_UP (entry->size, PGSIZE);
  for (size_t i = 0; i < page_count; i++)
    {
      void *upage = entry->addr + i * PGSIZE;
      struct spt_entry temp;
      temp.upage = upage;
      struct hash_elem *e = hash_delete (&proc->spt, &temp.hash_elem); 
      if (e != NULL)
        {
          struct spt_entry *spte = hash_entry (e, struct spt_entry, hash_elem);
          if (spte->frame != NULL)
            {
              if (pagedir_is_dirty (spte->pagedir, spte->upage) || spte->type == PAGE_SWAP)
                file_write_at (spte->file, spte->frame->kpage, spte->read_bytes, spte->file_offset);
              pagedir_clear_page (spte->pagedir, spte->upage);
              frame_free (spte->frame);
              if (spte->type == PAGE_SWAP && spte->swap_slot != 0)
                swap_free (spte->swap_slot);
            }
          else if (spte->type == PAGE_SWAP)
            {
              void *kpage = palloc_get_page (0);
              if (kpage != NULL)
                {
                  swap_read (spte->swap_slot, kpage);
                  file_write_at (spte->file, kpage, spte->read_bytes, spte->file_offset);
                  palloc_free_page (kpage);
                }
              swap_free (spte->swap_slot);
            }
          free (spte);
        }
    }
  file_close (entry->file);
  list_remove (&entry->list_elem);
  free (entry);
}

void
munmap_all (void)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;
  while (!list_empty (&proc->mmap_list))
    {
      struct mmap_entry *entry = list_entry (list_begin (&proc->mmap_list), struct mmap_entry, list_elem);
      mmap_unmap (entry->mapid);
    }
}

static struct mmap_entry *
mmap_lookup (struct list *mmap_list, mapid_t mapid)
{
  struct list_elem *e;
  for (e = list_begin (mmap_list); e != list_end (mmap_list); e = list_next (e))
    {
      struct mmap_entry *entry = list_entry (e, struct mmap_entry, list_elem);
      if (entry->mapid == mapid)
        return entry;
    }
  return NULL;
}