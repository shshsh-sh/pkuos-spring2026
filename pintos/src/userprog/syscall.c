#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

static int syscall_argc[] = {
  [SYS_HALT] = 0,
  [SYS_EXIT] = 1,
  [SYS_EXEC] = 1,
  [SYS_WAIT] = 1,
  [SYS_CREATE] = 2,
  [SYS_REMOVE] = 1,
  [SYS_OPEN] = 1,
  [SYS_FILESIZE] = 1,
  [SYS_READ] = 3,
  [SYS_WRITE] = 3,
  [SYS_SEEK] = 2,
  [SYS_TELL] = 1,
  [SYS_CLOSE] = 1
};

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_addr (const void *addr) 
{
  if (addr == NULL || !is_user_vaddr (addr))
    return false;
  struct thread *cur = thread_current ();
  if (pagedir_get_page (cur->pagedir, addr) != NULL)
    return true;
  return vm_load_page ((void *) addr, false);
}

static bool
is_all_valid_addr (const void *addr, size_t size) 
{
  const char *byte = addr;
  for (size_t i = 0; i < size; i++) {
    if (!is_valid_addr (byte + i)) {
      return false;
    }
  }
  return true;
}

static bool
is_valid_string (const char *str)
{
  if (!is_valid_addr (str))
    return false;
  
  size_t len = 0;
  while (true) {
    if (!is_valid_addr (str + len))
      return false;
    if (str[len] == '\0')
      break;
    len++;
  }
  return true;
}

static void
syscall_halt (void)
{
  shutdown_power_off ();
}

static void
syscall_exit (int status)
{
  process_exit (status);
}

static pid_t
syscall_exec (const char *cmd_line)
{
  if (!is_valid_string (cmd_line))
    syscall_exit (-1);
  
  return process_execute (cmd_line);
}

static int
syscall_wait (pid_t pid)
{
  return process_wait (pid);
}

static int
syscall_create (const char *file, unsigned initial_size)
{
  if (!is_valid_string (file))
    syscall_exit (-1);
    
  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

static int
syscall_remove (const char *file)
{
  if (!is_valid_string (file))
    syscall_exit (-1);
    
  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

static int
syscall_open (const char *file)
{
  if (!is_valid_string (file))
    syscall_exit (-1);

  struct thread *cur = thread_current ();
  struct process *proc = cur->process;
  
  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  struct file *f = filesys_open (file);
  if (f == NULL)
  {
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -1;
  }

  int fd = -2;
  for (int i = STDOUT_FILENO + 1; i < MAX_FD_COUNT; i++)
  {
    if (proc->fd_table[i] == NULL)
    {
      proc->fd_table[i] = f;
      fd = i;
      break;
    }
  }
  if (fd == -2)
  {
    file_close (f);
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -2;
  }

  lock_release (&proc->lock);
  lock_release (&filesys_lock);
  return fd;
}

static int
syscall_filesize (int fd)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;

  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  if (fd < 0 || fd >= MAX_FD_COUNT || proc->fd_table[fd] == NULL)
  {
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -1;
  }
  struct file *f = proc->fd_table[fd];
  lock_release (&proc->lock);
  int size = file_length (f);
  lock_release (&filesys_lock);
  return size;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  if (!is_valid_addr (buffer))
    syscall_exit (-1);

  if (fd == STDIN_FILENO)
  {
    int bytes_read = 0;
    uint8_t *buf = buffer;
    for (unsigned i = 0; i < size; i++)
    {
      int c = input_getc ();
      if (c == -1)
        break;
      if (!is_valid_addr (buf + i))
        syscall_exit (-1);
      buf[i] = (uint8_t) c;
      bytes_read++;
    }
    return bytes_read;
  }

  if (size > 0)
  {
    void *start = pg_round_down (buffer);
    void *end = pg_round_down (buffer + size - 1);
    struct thread *cur = thread_current ();
    for (void *p = start; p <= end; p += PGSIZE)
    {
      if (!vm_load_page (p, true))
        syscall_exit (-1);
      
      struct spt_entry *spte = spt_lookup (&cur->process->spt, p);
      if (spte == NULL || !spte->writable)
        syscall_exit (-1);
    }
  }

  struct thread *cur = thread_current ();
  struct process *proc = cur->process;

  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  if (fd < 0 || fd >= MAX_FD_COUNT || proc->fd_table[fd] == NULL)
  {
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -1;
  }
  struct file *f = proc->fd_table[fd];
  lock_release (&proc->lock);

  int bytes_read = file_read (f, buffer, size);
  lock_release (&filesys_lock);

  if (size > 0)
    vm_unpin_range (buffer, size);

  return bytes_read;
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  if (!is_all_valid_addr (buffer, size))
    syscall_exit (-1);

  if (size > 0)
  {
    void *start = pg_round_down (buffer);
    void *end = pg_round_down (buffer + size - 1);
    struct thread *cur = thread_current ();
    for (void *p = start; p <= end; p += PGSIZE)
    {
      if (!vm_load_page (p, true))
        syscall_exit (-1);
      
      struct spt_entry *spte = spt_lookup (&cur->process->spt, p);
      if (spte == NULL || !spte->writable)
        syscall_exit (-1);
    }
  }
    
  if (fd == STDOUT_FILENO)
  {
    putbuf (buffer, size);
    return size;
  }

  struct thread *cur = thread_current ();
  struct process *proc = cur->process;

  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  if (fd < 0 || fd >= MAX_FD_COUNT || proc->fd_table[fd] == NULL)
  {
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -1;
  }
  struct file *f = proc->fd_table[fd];
  lock_release (&proc->lock);

  int bytes_written = file_write (f, buffer, size);
  lock_release (&filesys_lock);

  if (size > 0)
    vm_unpin_range (buffer, size);

  return bytes_written;
}

static int
syscall_seek (int fd, unsigned position)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;

  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  if (fd < 0 || fd >= MAX_FD_COUNT || proc->fd_table[fd] == NULL)
  {
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -1;
  }
  struct file *f = proc->fd_table[fd];
  lock_release (&proc->lock);

  file_seek (f, position);
  lock_release (&filesys_lock);
  return 0;
}

static unsigned
syscall_tell (int fd)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;

  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  if (fd < 0 || fd >= MAX_FD_COUNT || proc->fd_table[fd] == NULL)
  {
    lock_release (&proc->lock);
    lock_release (&filesys_lock);
    return -1;
  }
  struct file *f = proc->fd_table[fd];
  lock_release (&proc->lock);

  unsigned position = file_tell (f);
  lock_release (&filesys_lock);
  return position;
}

static void
syscall_close (int fd)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;
  
  lock_acquire (&filesys_lock);
  lock_acquire (&proc->lock);
  
  if (fd >= 0 && fd < MAX_FD_COUNT && proc->fd_table[fd] != NULL)
    {
      struct file *f = proc->fd_table[fd];
      proc->fd_table[fd] = NULL;
      file_close (f);
    }
  
  lock_release (&proc->lock);
  lock_release (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *esp = f->esp;
  
  if (!is_all_valid_addr (esp, sizeof(int)))
    syscall_exit (-1);
  
  int syscall_num = *esp;
  if (syscall_num < 0 || (unsigned) syscall_num >= sizeof(syscall_argc)/sizeof(syscall_argc[0]))
    syscall_exit (-1);
  
  int argc = syscall_argc[syscall_num];
  if (!is_all_valid_addr (esp + 1, sizeof(int) * argc))
    syscall_exit (-1);
  
  switch (syscall_num) {
    case SYS_HALT:
      {
        syscall_halt ();
        break;
      }
    case SYS_EXIT:
      {
        int status = *(esp + 1);
        syscall_exit (status);
        break;
      }
    case SYS_EXEC:
      {
        f->eax = syscall_exec ((const char *) *(esp + 1));
        break;
      }
    case SYS_WAIT:
      {
        pid_t pid = *(esp + 1);
        f->eax = syscall_wait (pid);
        break;
      }
    case SYS_CREATE:
      {
        const char *file = (const char *) *(esp + 1);
        unsigned initial_size = (unsigned) *(esp + 2);
        f->eax = syscall_create (file, initial_size);
        break;
      }
    case SYS_REMOVE:
      {
        const char *file = (const char *) *(esp + 1);
        f->eax = syscall_remove (file);
        break;
      }
    case SYS_OPEN:
      {
        const char *file = (const char *) *(esp + 1);
        f->eax = syscall_open (file);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd = *(esp + 1);
        f->eax = syscall_filesize (fd);
        break;
      }
    case SYS_READ:
      {
        int fd = *(esp + 1);
        void *buffer = (void *) *(esp + 2);
        unsigned size = (unsigned) *(esp + 3);
        f->eax = syscall_read (fd, buffer, size);
        break;
      }
    case SYS_WRITE:
      {
        int fd = *(esp + 1);
        const void *buffer = (const void *) *(esp + 2);
        unsigned size = (unsigned) *(esp + 3);
        f->eax = syscall_write (fd, buffer, size);
        break;
      }
    case SYS_SEEK:
      {
        int fd = *(esp + 1);
        unsigned position = (unsigned) *(esp + 2);
        f->eax = syscall_seek (fd, position);
        break;
      }
    case SYS_TELL:
      {
        int fd = *(esp + 1);
        f->eax = syscall_tell (fd);
        break;
      }
    case SYS_CLOSE:
      {
        int fd = *(esp + 1);
        syscall_close (fd);
        break;
      }
    default:
      PANIC("Caught unknown syscall\n");
  }
}
