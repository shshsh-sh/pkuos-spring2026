#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"

#define MAX_FD_COUNT 128

typedef tid_t pid_t;

/**
 * In Pintos, each process has only one thread.
 * The `struct process` structure contains information about a process,
 * including its arguments, identifier, parent-child relationships,
 * exit status, synchronization primitives for waiting, and file descriptor table.
 */
struct process
  {
    char **argv;                           /**< Argument vector. */
    pid_t pid;                             /**< Process identifier. */
    struct list children;                  /**< List of child processes. */
    struct list_elem elem;                 /**< List element for child processes. */
    struct thread *parent;                 /**< Parent process. */
    int exit_status;                       /**< Exit status. */
    bool waited;                           /**< Whether the parent is waiting for this process. */
    bool exited;                           /**< Whether the process has exited. */
    bool loaded;                           /**< Whether the process has loaded successfully. */
    struct semaphore wait_sema;            /**< Semaphore for waiting on this process. */
    struct lock lock;                      /**< Lock for synchronizing access to this process. */
    struct file *fd_table[MAX_FD_COUNT];   /**< File descriptor table. */
    int fd_count;                          /**< Count of open file descriptors. */
    int ref_count;                         /**< Reference count for this process. */
    struct file *executable;               /**< The executable file of this process. */
    char *cmd_line_cpy;                    /**< A copy of the command line for this process. */
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void free_pagedir (void);
void process_exit (int status);
void init_process (struct process *proc, char **argv);
void process_activate (void);
void process_refcount_free (struct process *proc);

#endif /**< userprog/process.h */
