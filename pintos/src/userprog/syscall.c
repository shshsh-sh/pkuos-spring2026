#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"

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
  return addr != NULL && is_user_vaddr (addr) && pagedir_get_page (thread_current ()->pagedir, addr) != NULL;
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

static void
syscall_halt (void)
{
  shutdown_power_off ();
}

static void
syscall_exit (int status)
{
  struct thread *cur = thread_current ();
  struct process *proc = cur->process;
  // printf("[debug info] process_exit: name=%s, exit_status=%d\n", proc->argv[0], status);
  lock_acquire (&proc->lock);
  proc->exit_status = status;
  proc->exited = true;
  lock_release (&proc->lock);
  sema_up (&proc->wait_sema);
  process_exit ();
  process_refcount_free (proc);
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int *esp = f->esp;
  
  if (!is_all_valid_addr (esp, sizeof(int)))
    syscall_exit (-1);
  
  int syscall_num = *esp;
  if (syscall_num < 0 || syscall_num >= sizeof(syscall_argc)/sizeof(syscall_argc[0]))
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
    default:
      printf("[debug info] syscall_handler: syscall_num=%d not implemented\n", syscall_num);
      // syscall_exit (-1);
  }
}
