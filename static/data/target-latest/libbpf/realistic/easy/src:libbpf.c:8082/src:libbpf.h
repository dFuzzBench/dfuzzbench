/* Hide internal to user */
struct bpf_object;

struct bpf_object_open_opts {
	/* size of this struct, for forward/backward compatibility */
	size_t sz;
	/* object name override, if provided:
	 * - for object open from file, this will override setting object
	 *   name from file path's base name;
	 * - for object open from memory buffer, this will specify an object
	 *   name and will override default "<addr>-<buf-size>" name;
	 */
	const char *object_name;
	/* parse map definitions non-strictly, allowing extra attributes/data */
	bool relaxed_maps;
	/* maps that set the 'pinning' attribute in their definition will have
	 * their pin_path attribute set to a file in this directory, and be
	 * auto-pinned to that path on load; defaults to "/sys/fs/bpf".
	 */
	const char *pin_root_path;

	__u32 :32; /* stub out now removed attach_prog_fd */

	/* Additional kernel config content that augments and overrides
	 * system Kconfig for CONFIG_xxx externs.
	 */
	const char *kconfig;
	/* Path to the custom BTF to be used for BPF CO-RE relocations.
	 * This custom BTF completely replaces the use of vmlinux BTF
	 * for the purpose of CO-RE relocations.
	 * NOTE: any other BPF feature (e.g., fentry/fexit programs,
	 * struct_ops, etc) will need actual kernel BTF at /sys/kernel/btf/vmlinux.
	 */
	const char *btf_custom_path;
	/* Pointer to a buffer for storing kernel logs for applicable BPF
	 * commands. Valid kernel_log_size has to be specified as well and are
	 * passed-through to bpf() syscall. Keep in mind that kernel might
	 * fail operation with -ENOSPC error if provided buffer is too small
	 * to contain entire log output.
	 * See the comment below for kernel_log_level for interaction between
	 * log_buf and log_level settings.
	 *
	 * If specified, this log buffer will be passed for:
	 *   - each BPF progral load (BPF_PROG_LOAD) attempt, unless overridden
	 *     with bpf_program__set_log() on per-program level, to get
	 *     BPF verifier log output.
	 *   - during BPF object's BTF load into kernel (BPF_BTF_LOAD) to get
	 *     BTF sanity checking log.
	 *
	 * Each BPF command (BPF_BTF_LOAD or BPF_PROG_LOAD) will overwrite
	 * previous contents, so if you need more fine-grained control, set
	 * per-program buffer with bpf_program__set_log_buf() to preserve each
	 * individual program's verification log. Keep using kernel_log_buf
	 * for BTF verification log, if necessary.
	 */
	char *kernel_log_buf;
	size_t kernel_log_size;
	/*
	 * Log level can be set independently from log buffer. Log_level=0
	 * means that libbpf will attempt loading BTF or program without any
	 * logging requested, but will retry with either its own or custom log
	 * buffer, if provided, and log_level=1 on any error.
	 * And vice versa, setting log_level>0 will request BTF or prog
	 * loading with verbose log from the first attempt (and as such also
	 * for successfully loaded BTF or program), and the actual log buffer
	 * could be either libbpf's own auto-allocated log buffer, if
	 * kernel_log_buffer is NULL, or user-provided custom kernel_log_buf.
	 * If user didn't provide custom log buffer, libbpf will emit captured
	 * logs through its print callback.
	 */
	__u32 kernel_log_level;
	/* Path to BPF FS mount point to derive BPF token from.
	 *
	 * Created BPF token will be used for all bpf() syscall operations
	 * that accept BPF token (e.g., map creation, BTF and program loads,
	 * etc) automatically within instantiated BPF object.
	 *
	 * If bpf_token_path is not specified, libbpf will consult
	 * LIBBPF_BPF_TOKEN_PATH environment variable. If set, it will be
	 * taken as a value of bpf_token_path option and will force libbpf to
	 * either create BPF token from provided custom BPF FS path, or will
	 * disable implicit BPF token creation, if envvar value is an empty
	 * string. bpf_token_path overrides LIBBPF_BPF_TOKEN_PATH, if both are
	 * set at the same time.
	 *
	 * Setting bpf_token_path option to empty string disables libbpf's
	 * automatic attempt to create BPF token from default BPF FS mount
	 * point (/sys/fs/bpf), in case this default behavior is undesirable.
	 */
	const char *bpf_token_path;

	size_t :0;
};
#define bpf_object_open_opts__last_field bpf_token_path
/**
 * @brief **bpf_object__open()** creates a bpf_object by opening
 * the BPF ELF object file pointed to by the passed path and loading it
 * into memory.
 * @param path BPF object file path.
 * @return pointer to the new bpf_object; or NULL is returned on error,
 * error code is stored in errno
 */
LIBBPF_API struct bpf_object *bpf_object__open(const char *path);
/**
 * @brief **bpf_object__open_file()** creates a bpf_object by opening
 * the BPF ELF object file pointed to by the passed path and loading it
 * into memory.
 * @param path BPF object file path
 * @param opts options for how to load the bpf object, this parameter is
 * optional and can be set to NULL
 * @return pointer to the new bpf_object; or NULL is returned on error,
 * error code is stored in errno
 */
LIBBPF_API struct bpf_object *
bpf_object__open_file(const char *path, const struct bpf_object_open_opts *opts);
/**
 * @brief **bpf_object__open_mem()** creates a bpf_object by reading
 * the BPF objects raw bytes from a memory buffer containing a valid
 * BPF ELF object file.
 * @param obj_buf pointer to the buffer containing ELF file bytes
 * @param obj_buf_sz number of bytes in the buffer
 * @param opts options for how to load the bpf object
 * @return pointer to the new bpf_object; or NULL is returned on error,
 * error code is stored in errno
 */
LIBBPF_API struct bpf_object *
bpf_object__open_mem(const void *obj_buf, size_t obj_buf_sz,
		     const struct bpf_object_open_opts *opts);
/**
 * @brief **bpf_object__load()** loads BPF object into kernel.
 * @param obj Pointer to a valid BPF object instance returned by
 * **bpf_object__open*()** APIs
 * @return 0, on success; negative error code, otherwise, error code is
 * stored in errno
 */
LIBBPF_API int bpf_object__load(struct bpf_object *obj);
/**
 * @brief **bpf_object__close()** closes a BPF object and releases all
 * resources.
 * @param obj Pointer to a valid BPF object
 */
LIBBPF_API void bpf_object__close(struct bpf_object *obj);

/**
 * @brief **bpf_map__lookup_elem()** allows to lookup BPF map value
 * corresponding to provided key.
 * @param map BPF map to lookup element in
 * @param key pointer to memory containing bytes of the key used for lookup
 * @param key_sz size in bytes of key data, needs to match BPF map definition's **key_size**
 * @param value pointer to memory in which looked up value will be stored
 * @param value_sz size in byte of value data memory; it has to match BPF map
 * definition's **value_size**. For per-CPU BPF maps value size has to be
 * a product of BPF map value size and number of possible CPUs in the system
 * (could be fetched with **libbpf_num_possible_cpus()**). Note also that for
 * per-CPU values value size has to be aligned up to closest 8 bytes for
 * alignment reasons, so expected size is: `round_up(value_size, 8)
 * * libbpf_num_possible_cpus()`.
 * @flags extra flags passed to kernel for this operation
 * @return 0, on success; negative error, otherwise
 * **bpf_map__lookup_elem()** is high-level equivalent of
 * **bpf_map_lookup_elem()** API with added check for key and value size.
 */
LIBBPF_API int bpf_map__lookup_elem(const struct bpf_map *map,
				    const void *key, size_t key_sz,
				    void *value, size_t value_sz, __u64 flags);

/**
 * @brief **bpf_map__update_elem()** allows to insert or update value in BPF
 * map that corresponds to provided key.
 * @param map BPF map to insert to or update element in
 * @param key pointer to memory containing bytes of the key
 * @param key_sz size in bytes of key data, needs to match BPF map definition's **key_size**
 * @param value pointer to memory containing bytes of the value
 * @param value_sz size in byte of value data memory; it has to match BPF map
 * definition's **value_size**. For per-CPU BPF maps value size has to be
 * a product of BPF map value size and number of possible CPUs in the system
 * (could be fetched with **libbpf_num_possible_cpus()**). Note also that for
 * per-CPU values value size has to be aligned up to closest 8 bytes for
 * alignment reasons, so expected size is: `round_up(value_size, 8)
 * * libbpf_num_possible_cpus()`.
 * @flags extra flags passed to kernel for this operation
 * @return 0, on success; negative error, otherwise
 * **bpf_map__update_elem()** is high-level equivalent of
 * **bpf_map_update_elem()** API with added check for key and value size.
 */
LIBBPF_API int bpf_map__update_elem(const struct bpf_map *map,
				    const void *key, size_t key_sz,
				    const void *value, size_t value_sz, __u64 flags);

/**
 * @brief **bpf_map__delete_elem()** allows to delete element in BPF map that
 * corresponds to provided key.
 * @param map BPF map to delete element from
 * @param key pointer to memory containing bytes of the key
 * @param key_sz size in bytes of key data, needs to match BPF map definition's **key_size**
 * @flags extra flags passed to kernel for this operation
 * @return 0, on success; negative error, otherwise
 * **bpf_map__delete_elem()** is high-level equivalent of
 * **bpf_map_delete_elem()** API with added check for key size.
 */
LIBBPF_API int bpf_map__delete_elem(const struct bpf_map *map,
				    const void *key, size_t key_sz, __u64 flags);

/**
 * @brief **bpf_map__lookup_and_delete_elem()** allows to lookup BPF map value
 * corresponding to provided key and atomically delete it afterwards.
 * @param map BPF map to lookup element in
 * @param key pointer to memory containing bytes of the key used for lookup
 * @param key_sz size in bytes of key data, needs to match BPF map definition's **key_size**
 * @param value pointer to memory in which looked up value will be stored
 * @param value_sz size in byte of value data memory; it has to match BPF map
 * definition's **value_size**. For per-CPU BPF maps value size has to be
 * a product of BPF map value size and number of possible CPUs in the system
 * (could be fetched with **libbpf_num_possible_cpus()**). Note also that for
 * per-CPU values value size has to be aligned up to closest 8 bytes for
 * alignment reasons, so expected size is: `round_up(value_size, 8)
 * * libbpf_num_possible_cpus()`.
 * @flags extra flags passed to kernel for this operation
 * @return 0, on success; negative error, otherwise
 *
 * **bpf_map__lookup_and_delete_elem()** is high-level equivalent of
 * **bpf_map_lookup_and_delete_elem()** API with added check for key and value size.
 */
LIBBPF_API int bpf_map__lookup_and_delete_elem(const struct bpf_map *map,
					       const void *key, size_t key_sz,
					       void *value, size_t value_sz, __u64 flags);

/**
 * @brief **bpf_map__get_next_key()** allows to iterate BPF map keys by
 * fetching next key that follows current key.
 * @param map BPF map to fetch next key from
 * @param cur_key pointer to memory containing bytes of current key or NULL to
 * fetch the first key
 * @param next_key pointer to memory to write next key into
 * @param key_sz size in bytes of key data, needs to match BPF map definition's **key_size**
 * @return 0, on success; -ENOENT if **cur_key** is the last key in BPF map;
 * negative error, otherwise
 *
 * **bpf_map__get_next_key()** is high-level equivalent of
 * **bpf_map_get_next_key()** API with added check for key size.
 */
LIBBPF_API int bpf_map__get_next_key(const struct bpf_map *map,
				     const void *cur_key, void *next_key, size_t key_sz);

struct bpf_xdp_set_link_opts {
	size_t sz;
	int old_fd;
	size_t :0;
};
#define bpf_xdp_set_link_opts__last_field old_fd

struct bpf_xdp_attach_opts {
	size_t sz;
	int old_prog_fd;
	size_t :0;
};
#define bpf_xdp_attach_opts__last_field old_prog_fd

struct bpf_xdp_query_opts {
	size_t sz;
	__u32 prog_id;		/* output */
	__u32 drv_prog_id;	/* output */
	__u32 hw_prog_id;	/* output */
	__u32 skb_prog_id;	/* output */
	__u8 attach_mode;	/* output */
	__u64 feature_flags;	/* output */
	__u32 xdp_zc_max_segs;	/* output */
	size_t :0;
};
#define bpf_xdp_query_opts__last_field xdp_zc_max_segs

LIBBPF_API int bpf_xdp_attach(int ifindex, int prog_fd, __u32 flags,
			      const struct bpf_xdp_attach_opts *opts);
LIBBPF_API int bpf_xdp_detach(int ifindex, __u32 flags,
			      const struct bpf_xdp_attach_opts *opts);
LIBBPF_API int bpf_xdp_query(int ifindex, int flags, struct bpf_xdp_query_opts *opts);
LIBBPF_API int bpf_xdp_query_id(int ifindex, int flags, __u32 *prog_id);

/* TC related API */
enum bpf_tc_attach_point {
	BPF_TC_INGRESS = 1 << 0,
	BPF_TC_EGRESS  = 1 << 1,
	BPF_TC_CUSTOM  = 1 << 2,
};

#define BPF_TC_PARENT(a, b) 	\
	((((a) << 16) & 0xFFFF0000U) | ((b) & 0x0000FFFFU))

enum bpf_tc_flags {
	BPF_TC_F_REPLACE = 1 << 0,
};

struct bpf_tc_hook {
	size_t sz;
	int ifindex;
	enum bpf_tc_attach_point attach_point;
	__u32 parent;
	size_t :0;
};
#define bpf_tc_hook__last_field parent

struct bpf_tc_opts {
	size_t sz;
	int prog_fd;
	__u32 flags;
	__u32 prog_id;
	__u32 handle;
	__u32 priority;
	size_t :0;
};
#define bpf_tc_opts__last_field priority

LIBBPF_API int bpf_tc_hook_create(struct bpf_tc_hook *hook);
LIBBPF_API int bpf_tc_hook_destroy(struct bpf_tc_hook *hook);
LIBBPF_API int bpf_tc_attach(const struct bpf_tc_hook *hook,
			     struct bpf_tc_opts *opts);
LIBBPF_API int bpf_tc_detach(const struct bpf_tc_hook *hook,
			     const struct bpf_tc_opts *opts);
LIBBPF_API int bpf_tc_query(const struct bpf_tc_hook *hook,
			    struct bpf_tc_opts *opts);

/* Ring buffer APIs */
struct ring_buffer;
struct ring;
struct user_ring_buffer;

typedef int (*ring_buffer_sample_fn)(void *ctx, void *data, size_t size);

struct ring_buffer_opts {
	size_t sz; /* size of this struct, for forward/backward compatibility */
};

#define ring_buffer_opts__last_field sz

LIBBPF_API struct ring_buffer *
ring_buffer__new(int map_fd, ring_buffer_sample_fn sample_cb, void *ctx,
		 const struct ring_buffer_opts *opts);
LIBBPF_API void ring_buffer__free(struct ring_buffer *rb);
LIBBPF_API int ring_buffer__add(struct ring_buffer *rb, int map_fd,
				ring_buffer_sample_fn sample_cb, void *ctx);
LIBBPF_API int ring_buffer__poll(struct ring_buffer *rb, int timeout_ms);
LIBBPF_API int ring_buffer__consume(struct ring_buffer *rb);
LIBBPF_API int ring_buffer__consume_n(struct ring_buffer *rb, size_t n);
LIBBPF_API int ring_buffer__epoll_fd(const struct ring_buffer *rb);

/**
 * @brief **ring_buffer__ring()** returns the ringbuffer object inside a given
 * ringbuffer manager representing a single BPF_MAP_TYPE_RINGBUF map instance.
 *
 * @param rb A ringbuffer manager object.
 * @param idx An index into the ringbuffers contained within the ringbuffer
 * manager object. The index is 0-based and corresponds to the order in which
 * ring_buffer__add was called.
 * @return A ringbuffer object on success; NULL and errno set if the index is
 * invalid.
 */
LIBBPF_API struct ring *ring_buffer__ring(struct ring_buffer *rb,
					  unsigned int idx);

struct user_ring_buffer_opts {
	size_t sz; /* size of this struct, for forward/backward compatibility */
};

#define user_ring_buffer_opts__last_field sz
/**
 * @brief **user_ring_buffer__new()** creates a new instance of a user ring
 * buffer.
 *
 * @param map_fd A file descriptor to a BPF_MAP_TYPE_USER_RINGBUF map.
 * @param opts Options for how the ring buffer should be created.
 * @return A user ring buffer on success; NULL and errno being set on a
 * failure.
 */
LIBBPF_API struct user_ring_buffer *
user_ring_buffer__new(int map_fd, const struct user_ring_buffer_opts *opts);
/**
 * @brief **user_ring_buffer__reserve()** reserves a pointer to a sample in the
 * user ring buffer.
 * @param rb A pointer to a user ring buffer.
 * @param size The size of the sample, in bytes.
 * @return A pointer to an 8-byte aligned reserved region of the user ring
 * buffer; NULL, and errno being set if a sample could not be reserved.
 *
 * This function is *not* thread safe, and callers must synchronize accessing
 * this function if there are multiple producers.  If a size is requested that
 * is larger than the size of the entire ring buffer, errno will be set to
 * E2BIG and NULL is returned. If the ring buffer could accommodate the size,
 * but currently does not have enough space, errno is set to ENOSPC and NULL is
 * returned.
 *
 * After initializing the sample, callers must invoke
 * **user_ring_buffer__submit()** to post the sample to the kernel. Otherwise,
 * the sample must be freed with **user_ring_buffer__discard()**.
 */
LIBBPF_API void *user_ring_buffer__reserve(struct user_ring_buffer *rb, __u32 size);
/**
 * @brief **user_ring_buffer__reserve_blocking()** reserves a record in the
 * ring buffer, possibly blocking for up to @timeout_ms until a sample becomes
 * available.
 * @param rb The user ring buffer.
 * @param size The size of the sample, in bytes.
 * @param timeout_ms The amount of time, in milliseconds, for which the caller
 * should block when waiting for a sample. -1 causes the caller to block
 * indefinitely.
 * @return A pointer to an 8-byte aligned reserved region of the user ring
 * buffer; NULL, and errno being set if a sample could not be reserved.
 *
 * This function is *not* thread safe, and callers must synchronize
 * accessing this function if there are multiple producers
 *
 * If **timeout_ms** is -1, the function will block indefinitely until a sample
 * becomes available. Otherwise, **timeout_ms** must be non-negative, or errno
 * is set to EINVAL, and NULL is returned. If **timeout_ms** is 0, no blocking
 * will occur and the function will return immediately after attempting to
 * reserve a sample.
 *
 * If **size** is larger than the size of the entire ring buffer, errno is set
 * to E2BIG and NULL is returned. If the ring buffer could accommodate
 * **size**, but currently does not have enough space, the caller will block
 * until at most **timeout_ms** has elapsed. If insufficient space is available
 * at that time, errno is set to ENOSPC, and NULL is returned.
 *
 * The kernel guarantees that it will wake up this thread to check if
 * sufficient space is available in the ring buffer at least once per
 * invocation of the **bpf_ringbuf_drain()** helper function, provided that at
 * least one sample is consumed, and the BPF program did not invoke the
 * function with BPF_RB_NO_WAKEUP. A wakeup may occur sooner than that, but the
 * kernel does not guarantee this. If the helper function is invoked with
 * BPF_RB_FORCE_WAKEUP, a wakeup event will be sent even if no sample is
 * consumed.
 *
 * When a sample of size **size** is found within **timeout_ms**, a pointer to
 * the sample is returned. After initializing the sample, callers must invoke
 * **user_ring_buffer__submit()** to post the sample to the ring buffer.
 * Otherwise, the sample must be freed with **user_ring_buffer__discard()**.
 */
LIBBPF_API void *user_ring_buffer__reserve_blocking(struct user_ring_buffer *rb,
						    __u32 size,
						    int timeout_ms);

/**
 * @brief **user_ring_buffer__submit()** submits a previously reserved sample
 * into the ring buffer.
 * @param rb The user ring buffer.
 * @param sample A reserved sample.
 *
 * It is not necessary to synchronize amongst multiple producers when invoking
 * this function.
 */
LIBBPF_API void user_ring_buffer__submit(struct user_ring_buffer *rb, void *sample);

/**
 * @brief **user_ring_buffer__discard()** discards a previously reserved sample.
 * @param rb The user ring buffer.
 * @param sample A reserved sample.
 *
 * It is not necessary to synchronize amongst multiple producers when invoking
 * this function.
 */
LIBBPF_API void user_ring_buffer__discard(struct user_ring_buffer *rb, void *sample);

/**
 * @brief **user_ring_buffer__free()** frees a ring buffer that was previously
 * created with **user_ring_buffer__new()**.
 * @param rb The user ring buffer being freed.
 */
LIBBPF_API void user_ring_buffer__free(struct user_ring_buffer *rb);

/* Perf buffer APIs */
struct perf_buffer;

typedef void (*perf_buffer_sample_fn)(void *ctx, int cpu,
				      void *data, __u32 size);
typedef void (*perf_buffer_lost_fn)(void *ctx, int cpu, __u64 cnt);

/* common use perf buffer options */
struct perf_buffer_opts {
	size_t sz;
	__u32 sample_period;
	size_t :0;
};
#define perf_buffer_opts__last_field sample_period

/**
 * @brief **perf_buffer__new()** creates BPF perfbuf manager for a specified
 * BPF_PERF_EVENT_ARRAY map
 * @param map_fd FD of BPF_PERF_EVENT_ARRAY BPF map that will be used by BPF
 * code to send data over to user-space
 * @param page_cnt number of memory pages allocated for each per-CPU buffer
 * @param sample_cb function called on each received data record
 * @param lost_cb function called when record loss has occurred
 * @param ctx user-provided extra context passed into *sample_cb* and *lost_cb*
 * @return a new instance of struct perf_buffer on success, NULL on error with
 * *errno* containing an error code
 */
LIBBPF_API struct perf_buffer *
perf_buffer__new(int map_fd, size_t page_cnt,
		 perf_buffer_sample_fn sample_cb, perf_buffer_lost_fn lost_cb, void *ctx,
		 const struct perf_buffer_opts *opts);

enum bpf_perf_event_ret {
	LIBBPF_PERF_EVENT_DONE	= 0,
	LIBBPF_PERF_EVENT_ERROR	= -1,
	LIBBPF_PERF_EVENT_CONT	= -2,
};

struct perf_event_header;

typedef enum bpf_perf_event_ret
(*perf_buffer_event_fn)(void *ctx, int cpu, struct perf_event_header *event);

/* raw perf buffer options, giving most power and control */
struct perf_buffer_raw_opts {
	size_t sz;
	long :0;
	long :0;
	/* if cpu_cnt == 0, open all on all possible CPUs (up to the number of
	 * max_entries of given PERF_EVENT_ARRAY map)
	 */
	int cpu_cnt;
	/* if cpu_cnt > 0, cpus is an array of CPUs to open ring buffers on */
	int *cpus;
	/* if cpu_cnt > 0, map_keys specify map keys to set per-CPU FDs for */
	int *map_keys;
};
#define perf_buffer_raw_opts__last_field map_keys

struct perf_event_attr;

LIBBPF_API struct perf_buffer *
perf_buffer__new_raw(int map_fd, size_t page_cnt, struct perf_event_attr *attr,
		     perf_buffer_event_fn event_cb, void *ctx,
		     const struct perf_buffer_raw_opts *opts);

LIBBPF_API void perf_buffer__free(struct perf_buffer *pb);
LIBBPF_API int perf_buffer__epoll_fd(const struct perf_buffer *pb);
LIBBPF_API int perf_buffer__poll(struct perf_buffer *pb, int timeout_ms);
LIBBPF_API int perf_buffer__consume(struct perf_buffer *pb);
LIBBPF_API int perf_buffer__consume_buffer(struct perf_buffer *pb, size_t buf_idx);
LIBBPF_API size_t perf_buffer__buffer_cnt(const struct perf_buffer *pb);
LIBBPF_API int perf_buffer__buffer_fd(const struct perf_buffer *pb, size_t buf_idx);
/**
 * @brief **perf_buffer__buffer()** returns the per-cpu raw mmap()'ed underlying
 * memory region of the ring buffer.
 * This ring buffer can be used to implement a custom events consumer.
 * The ring buffer starts with the *struct perf_event_mmap_page*, which
 * holds the ring buffer management fields, when accessing the header
 * structure it's important to be SMP aware.
 * You can refer to *perf_event_read_simple* for a simple example.
 * @param pb the perf buffer structure
 * @param buf_idx the buffer index to retrieve
 * @param buf (out) gets the base pointer of the mmap()'ed memory
 * @param buf_size (out) gets the size of the mmap()'ed region
 * @return 0 on success, negative error code for failure
 */
LIBBPF_API int perf_buffer__buffer(struct perf_buffer *pb, int buf_idx, void **buf,
				   size_t *buf_size);

struct bpf_prog_linfo;
struct bpf_prog_info;

LIBBPF_API void bpf_prog_linfo__free(struct bpf_prog_linfo *prog_linfo);
LIBBPF_API struct bpf_prog_linfo *
bpf_prog_linfo__new(const struct bpf_prog_info *info);
LIBBPF_API const struct bpf_line_info *
bpf_prog_linfo__lfind_addr_func(const struct bpf_prog_linfo *prog_linfo,
				__u64 addr, __u32 func_idx, __u32 nr_skip);
LIBBPF_API const struct bpf_line_info *
bpf_prog_linfo__lfind(const struct bpf_prog_linfo *prog_linfo,
		      __u32 insn_off, __u32 nr_skip);

/*
 * Probe for supported system features
 *
 * Note that running many of these probes in a short amount of time can cause
 * the kernel to reach the maximal size of lockable memory allowed for the
 * user, causing subsequent probes to fail. In this case, the caller may want
 * to adjust that limit with setrlimit().
 */

/**
 * @brief **libbpf_probe_bpf_prog_type()** detects if host kernel supports
 * BPF programs of a given type.
 * @param prog_type BPF program type to detect kernel support for
 * @param opts reserved for future extensibility, should be NULL
 * @return 1, if given program type is supported; 0, if given program type is
 * not supported; negative error code if feature detection failed or can't be
 * performed
 *
 * Make sure the process has required set of CAP_* permissions (or runs as
 * root) when performing feature checking.
 */
LIBBPF_API int libbpf_probe_bpf_prog_type(enum bpf_prog_type prog_type, const void *opts);
/**
 * @brief **libbpf_probe_bpf_map_type()** detects if host kernel supports
 * BPF maps of a given type.
 * @param map_type BPF map type to detect kernel support for
 * @param opts reserved for future extensibility, should be NULL
 * @return 1, if given map type is supported; 0, if given map type is
 * not supported; negative error code if feature detection failed or can't be
 * performed
 *
 * Make sure the process has required set of CAP_* permissions (or runs as
 * root) when performing feature checking.
 */
LIBBPF_API int libbpf_probe_bpf_map_type(enum bpf_map_type map_type, const void *opts);
/**
 * @brief **libbpf_probe_bpf_helper()** detects if host kernel supports the
 * use of a given BPF helper from specified BPF program type.
 * @param prog_type BPF program type used to check the support of BPF helper
 * @param helper_id BPF helper ID (enum bpf_func_id) to check support for
 * @param opts reserved for future extensibility, should be NULL
 * @return 1, if given combination of program type and helper is supported; 0,
 * if the combination is not supported; negative error code if feature
 * detection for provided input arguments failed or can't be performed
 *
 * Make sure the process has required set of CAP_* permissions (or runs as
 * root) when performing feature checking.
 */
LIBBPF_API int libbpf_probe_bpf_helper(enum bpf_prog_type prog_type,
				       enum bpf_func_id helper_id, const void *opts);

/**
 * @brief **libbpf_num_possible_cpus()** is a helper function to get the
 * number of possible CPUs that the host kernel supports and expects.
 * @return number of possible CPUs; or error code on failure
 *
 * Example usage:
 *
 *     int ncpus = libbpf_num_possible_cpus();
 *     if (ncpus < 0) {
 *          // error handling
 *     }
 *     long values[ncpus];
 *     bpf_map_lookup_elem(per_cpu_map_fd, key, values);
 */
LIBBPF_API int libbpf_num_possible_cpus(void);
struct bpf_map_skeleton {
	const char *name;
	struct bpf_map **map;
	void **mmaped;
	struct bpf_link **link;
};
struct bpf_prog_skeleton {
	const char *name;
	struct bpf_program **prog;
	struct bpf_link **link;
};
struct bpf_object_skeleton {
	size_t sz; /* size of this struct, for forward/backward compatibility */

	const char *name;
	const void *data;
	size_t data_sz;

	struct bpf_object **obj;

	int map_cnt;
	int map_skel_sz; /* sizeof(struct bpf_map_skeleton) */
	struct bpf_map_skeleton *maps;

	int prog_cnt;
	int prog_skel_sz; /* sizeof(struct bpf_prog_skeleton) */
	struct bpf_prog_skeleton *progs;
};
LIBBPF_API int
bpf_object__open_skeleton(struct bpf_object_skeleton *s,
			  const struct bpf_object_open_opts *opts);
LIBBPF_API int bpf_object__load_skeleton(struct bpf_object_skeleton *s);
LIBBPF_API int bpf_object__attach_skeleton(struct bpf_object_skeleton *s);
LIBBPF_API void bpf_object__detach_skeleton(struct bpf_object_skeleton *s);
LIBBPF_API void bpf_object__destroy_skeleton(struct bpf_object_skeleton *s);
