# Project 3b: Virtual Memory

## Preliminaries

>Fill in your name and email address.

Junshi Liu 2400012942@stu.pku.edu.cn

>If you have any preliminary comments on your submission, notes for the TAs, please give them here.

还得是电动轮椅的那个爽。

>Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.

没有。

## Stack Growth

#### ALGORITHMS

>A1: Explain your heuristic for deciding whether a page fault for an
>invalid virtual address should cause the stack to be extended into
>the page that faulted.

首先要确保是栈区缺页，并且保证缺页地址离用户进程的栈指针不远。具体而言，缺页地址需要满足在 `[PHY_BASE - 0x800000, PHY_BASE]` 中，并且在 `%esp - 32` 以上。

## Memory Mapped Files

#### DATA STRUCTURES

>B1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

```c
// threads/thread.h
struct thread
  {
    ...
+   void *user_esp;                     /**< User stack pointer (for user processes). */
    ...
  };

// userprog/process.h
struct process
  {
    ...
+   mapid_t next_mapid;                    /**< Next memory mapping ID to assign. */
  };

// vm/mmap.h
typedef int mapid_t;

struct mmap_entry
  {
    mapid_t mapid;                   /**< Unique identifier for the memory mapping. */
    struct file *file;               /**< The file being mapped. */
    void *addr;                      /**< Starting address of the mapping in user space. */
    size_t size;                     /**< Size of the mapping in bytes. */
    struct list_elem list_elem;      /**< List element for the list of memory mappings in a process. */
  };

// vm/page.h
struct spt_entry
  {
    ...
+   mapid_t mapid;                /**< The memory mapping ID (if this page is part of a memory-mapped file). */
  };

// userprog/syscall.c
static int syscall_argc[] = {
  ...
+ [SYS_MMAP] = 2,
+ [SYS_MUNMAP] = 1
};
```

- `thread.h`：
    - `user_esp`：存储用户进程（线程）的栈指针，用于栈增长合法性判断；
- `process.h`：
    - `next_mapid`：为每次 mmap 系统调用分配唯一 `mapid_t`。
- `mmap.h`：
    - `mapid_t`：给 mapid 专门开的一个类型，不过本质是整数；
    - `mmap_entry`，表示一个内存映射文件区域：
        - `mapid`：映射对应的标识符；
        - `file`：被映射的文件对象指针；
        - `addr`：映射在用户空间的起始虚拟地址，必须页对齐；
        - `size`：映射的字节数；
        - `list_elem`：挂入 `mmap_list` 的成员。
- `page.h`：
    - `mapid`：标识该页所属的 mmap 区域（0 表示非 mmap 页）。
- `syscall.c`：
    - 就是给 mmap 和 munmap 定义参数个数。

什么？你问 `mmap_list` 在哪里？我在 Lab3a 设计了，当时没有用而已。

#### ALGORITHMS

>B2: Describe how memory mapped files integrate into your virtual
>memory subsystem.  Explain how the page fault and eviction
>processes differ between swap pages and other pages.

流程如下：

- 首先创建映射，为文件的每一个页创建一个 `PAGE_FILE` 类型的补充页表条目，记录偏移和文件指针等信息，并且往 `mmap_list` 插入条目；
- 缺页时开始从文件读取数据到帧即可，和任何页的加载路径相同；
- 解除映射时，若页脏则将内容再进行写回（这里，如果已加载到帧中，直接写帧不写回）。
- 进程退出时，需要解除所有映射。

缺页处理的差异：

- 换出无差异：所有页统一走 swap 路径；不过已经是 `PAGE_SWAP` 的话可以不用分配新的交换槽（除非原来为 0）。
- 读取数据来源有差异：
    - `PAGE_ZERO`：直接分配零页即可；
    - `PAGE_FILE`：需要从文件系统中读取；
    - `PAGE_SWAP`：从交换槽中读取，读取完之后还要释放这个槽位。

>B3: Explain how you determine whether a new file mapping overlaps
>any existing segment.

映射的时候，逐页检查补充页表中是否有对应条目。只要任何一页找到了已有条目，那么发生重叠，映射失败。

#### RATIONALE

>B4: Mappings created with "mmap" have similar semantics to those of
>data demand-paged from executables, except that "mmap" mappings are
>written back to their original files, not to swap.  This implies
>that much of their implementation can be shared.  Explain why your
>implementation either does or does not share much of the code for
>the two situations.

显然，二者在创建补充页表条目的时候做的事情几乎都一样，因为他们都是从文件中加载数据。缺页加载和换出当然也差不多。不过 mmap 页才会有回写逻辑，可执行页是不会被修改的。