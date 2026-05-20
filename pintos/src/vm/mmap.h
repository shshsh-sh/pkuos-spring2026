#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include <stddef.h>

struct file;

typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

struct mmap_entry
  {
    mapid_t mapid;                   /**< Unique identifier for the memory mapping. */
    struct file *file;               /**< The file being mapped. */
    void *addr;                      /**< Starting address of the mapping in user space. */
    size_t size;                     /**< Size of the mapping in bytes. */
    struct list_elem list_elem;      /**< List element for the list of memory mappings in a process. */
  };

mapid_t mmap_map (int fd, void *addr);
void mmap_unmap (mapid_t mapid);
void munmap_all (void);

#endif /* vm/mmap.h */