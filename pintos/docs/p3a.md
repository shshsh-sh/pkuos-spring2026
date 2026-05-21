# Project 3a: Virtual Memory

## Preliminaries

>Fill in your name and email address.

Junshi Liu <2400012942@stu.pku.edu.cn>

>If you have any preliminary comments on your submission, notes for the TAs, please give them here.

哈哈哈哈！！！！电动轮椅原来这么爽！！！！

>Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.

我用了 Opencode.

## Page Table Management

#### DATA STRUCTURES

>A1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

```c
// vm/frame.h
struct frame
  {
    void *kpage;                  /**< Kernel virtual address of the frame. */
    struct spt_entry *spte;       /**< The supplemental page table entry associated with this frame. */
    struct list_elem elem;        /**< List element for the frame table. */
    bool pinned;                  /**< Whether the frame is pinned (i.e., cannot be evicted). */
  };

// vm/frame.c

static struct list frame_table;
static struct lock frame_lock;
static struct list_elem *clock_hand;         // Pointer for the clock algorithm.

// vm/page.h
enum page_type
  {
    PAGE_ZERO,          /**< A page that is filled with zeros. */
    PAGE_FILE,          /**< A page that is backed by a file. */
    PAGE_SWAP           /**< A page that is currently swapped out to disk. */
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
  };

// vm/swap.c

static struct block *swap_device;          /* Swap device. */
static struct bitmap *swap_bitmap;         /* Bitmap to track swap slots. */
static struct lock swap_lock;              /* Lock to protect swap operations. */

// userprog/process.h
struct process
  {
    ...
+   struct hash spt;                       /**< Supplemental page table for this process. */
+   struct list mmap_list;                 /**< List of memory-mapped files for this process. */
  };
```

- `struct frame`：物理帧结构体，管理物理内存页的分配与回收。
  - `kpage`：帧在内核空间中的虚拟地址，用于内核直接访问该物理页。
  - `spte`：指向该帧对应的补充页表项，便于换出时反向查找。
  - `elem`：链表元素，用于将帧加入全局帧表，供时钟算法遍历。
  - `pinned`：标记帧是否被固定，固定帧不会被换出。
- `frame.c`：
    - `frame_table`：物理帧的表。
    - `frame_lock`：保护 `frame_table` 的锁。
    - `clock_hand`：Clock 替换策略的指针。
- `enum page_type`：页的类型枚举，决定缺页时如何加载数据。
    - `PAGE_ZERO`：零填充页（如栈页、BSS），缺页时分配零初始化帧。
    - `PAGE_FILE`：文件映射页（如 ELF 段、mmap），缺页时从文件读取。
    - `PAGE_SWAP`：已换出到交换分区的页，缺页时从 swap 读回。
- `struct spt_entry`：补充页表项，记录进程每个虚拟页的元数据和映射状态。
    - `hash_elem`：哈希表元素，用于将页表项加入进程的补充页表哈希。
    - `upage`：用户态虚拟地址，哈希查找的键值。
    - `type`：页的类型，决定页的存储位置和加载方式。
    - `writable`：页是否可写，用于设置页表项的标志位。
    - `file`：文件映射页的后备文件指针（PAGE_FILE 时使用）。
    - `file_offset`：页在文件中的起始偏移量。
    - `read_bytes`：从文件实际读取的字节数（最后一页可能不满 PGSIZE）。
    - `zero_bytes`：文件内容后需补零的字节数（PGSIZE - read_bytes）。
    - `swap_slot`：交换分区的槽号（PAGE_SWAP 时使用）。
    - `frame`：页当前占用的物理帧指针，NULL 表示不在内存中。
    - `pagedir`：拥有该页的进程页目录，用于访问和脏位的操作。
- `swap.c`：
    - `swap_device`：交换分区对应的块设备指针，用于实际的磁盘读写操作。
    - `swap_bitmap`：位图，每个 bit 标记一个 swap slot 是否被占用。
    - `swap_lock`：保护 `swap_bitmap` 的锁。
- `struct process`：
    - `spt`：进程的补充页表哈希表，存储所有用户虚拟页的表项。
    - `mmap_list`：内存映射文件列表，记录每个 mmap 区域用于遍历写回。

#### ALGORITHMS

>A2: In a few paragraphs, describe your code for accessing the data
>stored in the SPT about a given page.

首先，根据给定的用户虚拟地址，首先进行页对齐，然后创建一个临时对象，只填充其 `upage` 成员，然后在哈希表中查找对应成员。如果找到了就直接返回这个结果，否则返回 `NULL` 表示未找到。

>A3: How does your code coordinate accessed and dirty bits between
>kernel and user virtual addresses that alias a single frame, or
>alternatively how do you avoid the issue?

在 Pintos 中，内核空间是物理内存的恒等映射，每个物理帧只有最多一个内核虚拟地址。内核读写物理帧是直接通过 `frame->kpage` 修改，因此不会走页表的逻辑。而用户代码在访问页面时，CPU 会自动设置 A/D 位。这两条路径互不干扰，各自独立，因此不会存在这个问题。

#### SYNCHRONIZATION

>A4: When two user processes both need a new frame at the same time,
>how are races avoided?

每次进程在尝试进行帧分配回收替换等操作时，都会用 `frame_lock` 进行保护。这样每次只会有最多一个进程在尝试帧分配，自然就不会有竞争。

#### RATIONALE

>A5: Why did you choose the data structure(s) that you did for
>representing virtual-to-physical mappings?

SPT 使用哈希表，是因为每次只用一个键值查找 SPT 表项。这样可以快速定位；   
帧表使用链表：这是为了方便实现 Clock 替换策略。

## Paging To And From Disk

#### DATA STRUCTURES

>B1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

在 A1 中已经写完了，对于 Lab3a 不需要更多的定义。

#### ALGORITHMS

>B2: When a frame is required but none is free, some frame must be
>evicted.  Describe your code for choosing a frame to evict.

使用 Clock 策略决定换出的帧。使用时钟指针，每次换出时从上一次停留的位置继续在帧表上循环前进，如果为空或已在末尾则回到开头。

如果当前帧未被固定，并且关联了用户页，那么检查访问位。如果：

- 被访问过，那么清空访问位（Second Chance）；
- 否则逐出。

然后进行正常的逐出逻辑。清除 PTE，根据类型将页写入 swap 或者释放，将物理页返回以供新帧使用。

>B3: When a process P obtains a frame that was previously used by a
>process Q, how do you adjust the page table (and any other data
>structures) to reflect the frame Q no longer has?

假设选中的牺牲帧关联进程 Q 的页表项。我们首先清空 Q 页目录中对应虚拟地址的表项，解除虚拟页到物理帧的映射。然后修改帧与补充页表之间的关联，表示该页不占用任何物理帧。然后保存数据，如果页为脏则写入 swap 并记录相关信息。最后物理页被 P 复用，并修改补充页表建立和 P 新的联系。

#### SYNCHRONIZATION

>B5: Explain the basics of your VM synchronization design.  In
>particular, explain how it prevents deadlock.  (Refer to the
>textbook for an explanation of the necessary conditions for
>deadlock.)

帧表，交换槽位图，文件系统和进程本身都有锁进行保护。保证加锁顺序的情况下当然不会出现死锁。

>B6: A page fault in process P can cause another process Q's frame
>to be evicted.  How do you ensure that Q cannot access or modify
>the page during the eviction process?  How do you avoid a race
>between P evicting Q's frame and Q faulting the page back in?

Q 被逐出时加了锁，我们在这个条件下清除了页表项，在此之后 Q 访问一定会触发缺页异常了，因此可以保证 Q 不会再访问该页。

与此同时，我们保证 Q 被换出的操作是原子的即可避免竞争。

>B7: Suppose a page fault in process P causes a page to be read from
>the file system or swap.  How do you ensure that a second process Q
>cannot interfere by e.g. attempting to evict the frame while it is
>still being read in?

我们将 P 正在使用的页面对应的帧固定即可。

>B8: Explain how you handle access to paged-out pages that occur
>during system calls.  Do you use page faults to bring in pages (as
>in user programs), or do you have a mechanism for "locking" frames
>into physical memory, or do you use some other design?  How do you
>gracefully handle attempted accesses to invalid virtual addresses?

系统调用中，我们按需加载，加载的页对应的帧全部固定，这样可以保证不被换出。

#### RATIONALE

>B9: A single lock for the whole VM system would make
>synchronization easy, but limit parallelism.  On the other hand,
>using many locks complicates synchronization and raises the
>possibility for deadlock but allows for high parallelism.  Explain
>where your design falls along this continuum and why you chose to
>design it this way.

我们全部用若干把大锁来锁住所有需要保护的数据结构。最主要还是实现简单。当然也有 Pintos 拉完了的原因，以至于这样写已经完全够用了。