#include <linux/suspend.h>
#include <linux/suspend_ioctls.h>
#include <linux/utsname.h>
#include <linux/freezer.h>

#if 0
#define sleep_trace(fmt, ...)	pr_info("[%s][%s]" fmt, "SLEEPTRACE", __func__, ##__VA_ARGS__);
#else
#define sleep_trace(fmt, ...)	((void)0)
#endif

struct swsusp_info {
	struct new_utsname	uts;
	u32			version_code;
	unsigned long		num_physpages;
	int			cpus;
	unsigned long		image_pages;
	unsigned long		pages;
	unsigned long		size;
} __attribute__((aligned(PAGE_SIZE)));

#ifdef CONFIG_HIBERNATION
/* kernel/power/snapshot.c */
extern void __init hibernate_reserved_size_init(void);
extern void __init hibernate_image_size_init(void);

#ifdef CONFIG_ARCH_HIBERNATION_HEADER
/* Maximum size of architecture specific data in a hibernation header */
#define MAX_ARCH_HEADER_SIZE	(sizeof(struct new_utsname) + 4)

extern int arch_hibernation_header_save(void *addr, unsigned int max_size);
extern int arch_hibernation_header_restore(void *addr);

static inline int init_header_complete(struct swsusp_info *info)
{
	return arch_hibernation_header_save(info, MAX_ARCH_HEADER_SIZE);
}

static inline char *check_image_kernel(struct swsusp_info *info)
{
	return arch_hibernation_header_restore(info) ?
			"architecture specific data" : NULL;
}
#else
extern char *check_image_kernel(struct swsusp_info *info);
#endif /* CONFIG_ARCH_HIBERNATION_HEADER */
extern int init_header(struct swsusp_info *info);

extern char resume_file[256];
/*
 * Keep some memory free so that I/O operations can succeed without paging
 * [Might this be more than 4 MB?]
 */
#define PAGES_FOR_IO	((4096 * 1024) >> PAGE_SHIFT)

/*
 * Keep 1 MB of memory free so that device drivers can allocate some pages in
 * their .suspend() routines without breaking the suspend to disk.
 */
#define SPARE_PAGES	((1024 * 1024) >> PAGE_SHIFT)

/* kernel/power/hibernate.c */
extern bool freezer_test_done;

extern int hibernation_snapshot(int platform_mode);
extern int hibernation_restore(int platform_mode);
extern int hibernation_platform_enter(void);
extern void platform_recover(int platform_mode);

#else /* !CONFIG_HIBERNATION */

static inline void hibernate_reserved_size_init(void) {}
static inline void hibernate_image_size_init(void) {}
#endif /* !CONFIG_HIBERNATION */

extern int pfn_is_nosave(unsigned long);

#define power_attr(_name) \
static struct kobj_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

extern struct pbe *restore_pblist;

/* Preferred image size in bytes (default 500 MB) */
extern unsigned long image_size;
/* Size of memory reserved for drivers (default SPARE_PAGES x PAGE_SIZE) */
extern unsigned long reserved_size;
extern int in_suspend;
extern dev_t swsusp_resume_device;
extern sector_t swsusp_resume_block;

extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);

extern int create_basic_memory_bitmaps(void);
extern void free_basic_memory_bitmaps(void);
extern int hibernate_preallocate_memory(void);

/**
 *	Auxiliary structure used for reading the snapshot image data and
 *	metadata from and writing them to the list of page backup entries
 *	(PBEs) which is the main data structure of swsusp.
 *
 *	Using struct snapshot_handle we can transfer the image, including its
 *	metadata, as a continuous sequence of bytes with the help of
 *	snapshot_read_next() and snapshot_write_next().
 *
 *	The code that writes the image to a storage or transfers it to
 *	the user land is required to use snapshot_read_next() for this
 *	purpose and it should not make any assumptions regarding the internal
 *	structure of the image.  Similarly, the code that reads the image from
 *	a storage or transfers it from the user land is required to use
 *	snapshot_write_next().
 *
 *	This may allow us to change the internal structure of the image
 *	in the future with considerably less effort.
 */

struct snapshot_handle {
	unsigned int	cur;	/* number of the block of PAGE_SIZE bytes the
				 * next operation will refer to (ie. current)
				 */
	void		*buffer;	/* address of the block to read from
					 * or write to
					 */
	int		sync_read;	/* Set to one to notify the caller of
					 * snapshot_write_next() that it may
					 * need to call wait_on_bio_chain()
					 */
};

/* This macro returns the address from/to which the caller of
 * snapshot_read_next()/snapshot_write_next() is allowed to
 * read/write data after the function returns
 */
#define data_of(handle)	((handle).buffer)

extern unsigned int snapshot_additional_pages(struct zone *zone);
extern unsigned long snapshot_get_image_size(void);
extern int snapshot_read_next(struct snapshot_handle *handle);
extern int snapshot_write_next(struct snapshot_handle *handle);
extern void snapshot_write_finalize(struct snapshot_handle *handle);
extern int snapshot_image_loaded(struct snapshot_handle *handle);

/* If unset, the snapshot device cannot be open. */
extern atomic_t snapshot_device_available;

extern sector_t alloc_swapdev_block(int swap);
extern void free_all_swap_pages(int swap);
extern int swsusp_swap_in_use(void);

/*
 * Flags that can be passed from the hibernatig hernel to the "boot" kernel in
 * the image header.
 */
#define SF_PLATFORM_MODE	1
#define SF_NOCOMPRESS_MODE	2
#define SF_CRC32_MODE	        4

/* kernel/power/hibernate.c */
extern int swsusp_check(void);
extern void swsusp_free(void);
extern int swsusp_read(unsigned int *flags_p);
extern int swsusp_write(unsigned int flags);
extern void swsusp_close(fmode_t);

/* kernel/power/block_io.c */
extern struct block_device *hib_resume_bdev;

extern int hib_bio_read_page(pgoff_t page_off, void *addr,
		struct bio **bio_chain);
extern int hib_bio_write_page(pgoff_t page_off, void *addr,
		struct bio **bio_chain);
extern int hib_wait_on_bio_chain(struct bio **bio_chain);

struct timeval;
/* kernel/power/swsusp.c */
extern void swsusp_show_speed(struct timeval *, struct timeval *,
				unsigned int, char *);

#ifdef CONFIG_SUSPEND
/* kernel/power/suspend.c */
extern const char *const pm_states[];

extern bool valid_state(suspend_state_t state);
extern int suspend_devices_and_enter(suspend_state_t state);
extern int enter_state(suspend_state_t state);
#else /* !CONFIG_SUSPEND */
static inline int suspend_devices_and_enter(suspend_state_t state)
{
	return -ENOSYS;
}
static inline int enter_state(suspend_state_t state) { return -ENOSYS; }
static inline bool valid_state(suspend_state_t state) { return false; }
#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_PM_TEST_SUSPEND
/* kernel/power/suspend_test.c */
extern void suspend_test_start(void);
extern void suspend_test_finish(const char *label);
#else /* !CONFIG_PM_TEST_SUSPEND */
static inline void suspend_test_start(void) {}
static inline void suspend_test_finish(const char *label) {}
#endif /* !CONFIG_PM_TEST_SUSPEND */

#ifdef CONFIG_PM_SLEEP
/* kernel/power/main.c */
extern int pm_notifier_call_chain(unsigned long val);
#endif

#ifdef CONFIG_HIGHMEM
int restore_highmem(void);
#else
static inline unsigned int count_highmem_pages(void) { return 0; }
static inline int restore_highmem(void) { return 0; }
#endif

/*
 * Suspend test levels
 */
enum {
	/* keep first */
	TEST_NONE,
	TEST_CORE,
	TEST_CPUS,
	TEST_PLATFORM,
	TEST_DEVICES,
	TEST_FREEZER,
	/* keep last */
	__TEST_AFTER_LAST
};

#define TEST_FIRST	TEST_NONE
#define TEST_MAX	(__TEST_AFTER_LAST - 1)

extern int pm_test_level;

#ifdef CONFIG_SUSPEND_FREEZER
static inline int suspend_freeze_processes(void)
{
	int error;

	error = freeze_processes();
	/*
	 * freeze_processes() automatically thaws every task if freezing
	 * fails. So we need not do anything extra upon error.
	 */
	if (error)
		return error;

	error = freeze_kernel_threads();
	/*
	 * freeze_kernel_threads() thaws only kernel threads upon freezing
	 * failure. So we have to thaw the userspace tasks ourselves.
	 */
	if (error)
		thaw_processes();

	return error;
}

static inline void suspend_thaw_processes(void)
{
	thaw_processes();
}
#else
static inline int suspend_freeze_processes(void)
{
	return 0;
}

static inline void suspend_thaw_processes(void)
{
}
#endif

extern struct page *saveable_page(struct zone *z, unsigned long p);
#ifdef CONFIG_HIGHMEM
extern struct page *saveable_highmem_page(struct zone *z, unsigned long p);
#else
static
inline struct page *saveable_highmem_page(struct zone *z, unsigned long p)
{
	return NULL;
}
#endif

#define PBES_PER_PAGE (PAGE_SIZE / sizeof(struct pbe))
extern struct list_head nosave_regions;

/**
 *	This structure represents a range of page frames the contents of which
 *	should not be saved during the suspend.
 */

struct nosave_region {
	struct list_head list;
	unsigned long start_pfn;
	unsigned long end_pfn;
};

#define BM_END_OF_MAP	(~0UL)

#define BM_BITS_PER_BLOCK	(PAGE_SIZE * BITS_PER_BYTE)

struct bm_block {
	struct list_head hook;		/* hook into a list of bitmap blocks */
	unsigned long start_pfn;	/* pfn represented by the first bit */
	unsigned long end_pfn;	/* pfn represented by the last bit plus 1 */
	unsigned long *data;	/* bitmap representing pages */
};

/* struct bm_position is used for browsing memory bitmaps */

struct bm_position {
	struct bm_block *block;
	int bit;
};

struct memory_bitmap {
	struct list_head blocks;	/* list of bitmap blocks */
	struct linked_page *p_list;	/* list of pages used to store zone
					 * bitmap objects and bitmap block
					 * objects
					 */
	struct bm_position *states;	/* most recently used bit position */
	int num_states;			/* when iterating over a bitmap and
					 * number of states we support.
					 */
};

extern int memory_bm_create(struct memory_bitmap *bm, gfp_t gfp_mask,
		int safe_needed);
extern int memory_bm_create_index(struct memory_bitmap *bm, gfp_t gfp_mask,
		int safe_needed, int index);
extern void memory_bm_free(struct memory_bitmap *bm, int clear_nosave_free);
extern void memory_bm_set_bit(struct memory_bitmap *bm, unsigned long pfn);
extern void memory_bm_clear_bit(struct memory_bitmap *bm, unsigned long pfn);
extern void memory_bm_clear_bit_index(struct memory_bitmap *bm, unsigned long pfn, int index);
extern int memory_bm_test_bit(struct memory_bitmap *bm, unsigned long pfn);
extern int memory_bm_test_bit_index(struct memory_bitmap *bm, unsigned long pfn, int index);
extern unsigned long memory_bm_next_pfn(struct memory_bitmap *bm);
extern unsigned long memory_bm_next_pfn_index(struct memory_bitmap *bm,
		int index);
extern void memory_bm_position_reset(struct memory_bitmap *bm);
extern void memory_bm_clear(struct memory_bitmap *bm);
extern void memory_bm_copy(struct memory_bitmap *source,
		struct memory_bitmap *dest);
extern void memory_bm_dup(struct memory_bitmap *source,
		struct memory_bitmap *dest);
extern int memory_bm_set_iterators(struct memory_bitmap *bm, int number);

#ifdef CONFIG_TOI
struct toi_module_ops;
extern int memory_bm_read(struct memory_bitmap *bm, int (*rw_chunk)
	(int rw, struct toi_module_ops *owner, char *buffer, int buffer_size));
extern int memory_bm_write(struct memory_bitmap *bm, int (*rw_chunk)
	(int rw, struct toi_module_ops *owner, char *buffer, int buffer_size));
#endif

#ifdef CONFIG_PM_AUTOSLEEP

/* kernel/power/autosleep.c */
extern int pm_autosleep_init(void);
extern int pm_autosleep_lock(void);
extern void pm_autosleep_unlock(void);
extern suspend_state_t pm_autosleep_state(void);
extern int pm_autosleep_set_state(suspend_state_t state);

#else /* !CONFIG_PM_AUTOSLEEP */

static inline int pm_autosleep_init(void) { return 0; }
static inline int pm_autosleep_lock(void) { return 0; }
static inline void pm_autosleep_unlock(void) {}
static inline suspend_state_t pm_autosleep_state(void) { return PM_SUSPEND_ON; }

#endif /* !CONFIG_PM_AUTOSLEEP */

#ifdef CONFIG_EARLYSUSPEND
/* kernel/power/earlysuspend.c */
void request_suspend_state(suspend_state_t state);
suspend_state_t get_suspend_state(void);
#endif

#ifdef CONFIG_PM_WAKELOCKS

/* kernel/power/wakelock.c */
extern ssize_t pm_show_wakelocks(char *buf, bool show_active);
extern int pm_wake_lock(const char *buf);
extern int pm_wake_unlock(const char *buf);

#endif /* !CONFIG_PM_WAKELOCKS */
