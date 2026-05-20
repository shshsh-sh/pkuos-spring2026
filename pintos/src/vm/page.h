#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "devices/block.h"
#include "filesys/off_t.h"
#include "vm/mmap.h"

enum page_type
  {
    PAGE_ZERO,          /**< A page that is filled with zeros. */
    PAGE_FILE,          /**< A page that is backed by a file. */
    PAGE_SWAP,          /**< A page that is currently swapped out to disk. */
  };

struct spt_entry
  {
    struct hash_elem hash_elem;   /**< Hash element for the supplemental page table. */
    void *upage;                  /**< User virtual address. */
    enum page_type type;          /**< Type of the page (frame, file, or swap). */
    bool writable;                /**< Whether the page is writable. */
    struct file *file;            /**< The file backing this page (if type is PAGE_FILE). */
    off_t file_offset;            /**< Offset within the file (if type is PAGE_FILE). */
    size_t read_bytes;            /**< Number of bytes to read from the file (if type is PAGE_FILE). */
    size_t zero_bytes;            /**< Number of bytes to zero (if type is PAGE_FILE). */
    block_sector_t swap_slot;     /**< Swap slot index (if type is PAGE_SWAP). */
    struct frame *frame;          /**< The frame this page is loaded into (NULL if not in memory). */
    uint32_t *pagedir;            /**< The page directory of the owning process. */
    mapid_t mapid;                /**< The memory mapping ID (if this page is part of a memory-mapped file). */
  };

unsigned spt_hash_func(const struct hash_elem *e, void *aux);
bool spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
struct spt_entry *spt_lookup(struct hash *spt, const void *upage);
bool spt_insert_page(struct hash *spt, struct spt_entry *page);
void spt_destroy_func(struct hash_elem *e, void *aux);

bool vm_load_page (void *upage, bool pin);
bool vm_load_and_pin_range (void *addr, size_t size);
void vm_unpin_range (void *addr, size_t size);

#endif /* vm/page.h */