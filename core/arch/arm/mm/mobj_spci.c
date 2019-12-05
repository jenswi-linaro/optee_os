// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2016-2019, Linaro Limited
 */

#include <assert.h>
#include <bitstring.h>
#include <initcall.h>
#include <keep.h>
#include <kernel/refcount.h>
#include <kernel/spinlock.h>
#include <mm/mobj.h>
#include <sys/queue.h>

struct mobj_spci {
	struct mobj mobj;
	SLIST_ENTRY(mobj_spci) link;
	uint32_t cookie;
	tee_mm_entry_t *mm;
	struct refcount mapcount;
	uint16_t page_offset;
	bool registered_by_cookie;
	bool unregistered_by_cookie;
	paddr_t pages[];
};


SLIST_HEAD(mobj_spci_head, mobj_spci);

#define NUM_SHMS 64
static bitstr_t bit_decl(shm_bits, NUM_SHMS);

static struct mobj_spci_head shm_head =
	SLIST_HEAD_INITIALIZER(shm_head);
static struct mobj_spci_head shm_inactive_head =
	SLIST_HEAD_INITIALIZER(shm_inactive_head);

static unsigned int shm_lock = SPINLOCK_UNLOCK;


static const struct mobj_ops mobj_spci_ops __rodata_unpaged;

static struct mobj_spci *to_mobj_spci(struct mobj *mobj)
{
	assert(mobj->ops == &mobj_spci_ops);
	return container_of(mobj, struct mobj_spci, mobj);
}

static struct mobj_spci *to_mobj_spci_may_fail(struct mobj *mobj)
{
	if (mobj && mobj->ops != &mobj_spci_ops)
		return NULL;

	return container_of(mobj, struct mobj_spci, mobj);
}

static size_t shm_size(size_t num_pages)
{
	size_t s = 0;

	if (MUL_OVERFLOW(sizeof(paddr_t), num_pages, &s))
		return 0;
	if (ADD_OVERFLOW(sizeof(struct mobj_spci), s, &s))
		return 0;
	return s;
}

struct mobj_spci *mobj_spci_new(unsigned int num_pages,
				unsigned int page_offset)
{
	uint32_t exceptions = 0;
	struct mobj_spci *ms = NULL;
	size_t s = 0;
	int i = 0;

	if (!num_pages)
		return NULL;

	s = shm_size(num_pages);
	if (!s)
		return NULL;
	ms = calloc(1, s);
	if (!ms)
		return NULL;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	bit_ffc(shm_bits, NUM_SHMS, &i);
	if (i != -1) {
		bit_set(shm_bits, i);
		/* + 1 to avoid a cookie value 0 */
		ms->cookie = i + 1;
	}
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	if (i == -1) {
		free(ms);
		return NULL;
	}

	ms->mobj.ops = &mobj_spci_ops;
	ms->mobj.size = num_pages * SMALL_PAGE_SIZE;
	ms->mobj.phys_granule = SMALL_PAGE_SIZE;
	ms->page_offset = page_offset;
	refcount_set(&ms->mobj.refc, 0);

	return ms;
}

static bool cmp_cookie(struct mobj_spci *ms, vaddr_t cookie)
{
	return ms->cookie == cookie;
}

static bool cmp_ptr(struct mobj_spci *ms, vaddr_t ptr)
{
	return ms == (void *)ptr;
}

static struct mobj_spci *pop_from_list(struct mobj_spci_head *head,
				       bool (*cmp_func)(struct mobj_spci *ms,
							vaddr_t val),
				       vaddr_t val)
{
	struct mobj_spci *ms = SLIST_FIRST(head);
	struct mobj_spci *p = NULL;

	if (!ms)
		return NULL;

	if (cmp_func(ms, val)) {
		SLIST_REMOVE_HEAD(head, link);
		return ms;
	}

	while (true) {
		p = SLIST_NEXT(ms, link);
		if (!p)
			return NULL;
		if (cmp_func(p, val)) {
			SLIST_REMOVE_AFTER(ms, link);
			return p;
		}
		ms = p;
	}
}

static struct mobj_spci *find_in_list(struct mobj_spci_head *head,
				      bool (*cmp_func)(struct mobj_spci *ms,
						       vaddr_t val),
				      vaddr_t val)
{
	struct mobj_spci *ms = NULL;

	SLIST_FOREACH(ms, head, link)
		if (cmp_func(ms, val))
			return ms;

	return NULL;
}

void mobj_spci_delete(struct mobj_spci *ms)
{
	int i = ms->cookie - 1;
	uint32_t exceptions = 0;

	assert(i >= 0 && i < NUM_SHMS);

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	assert(bit_test(shm_bits, i));
	bit_clear(shm_bits, i);
	assert(!ms->mm);
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	free(ms);
}

uint32_t mobj_spci_add_pages_at(struct mobj_spci *ms, unsigned int *idx,
				paddr_t pa, unsigned int num_pages)
{
	unsigned int n;
	size_t tot_page_count = ms->mobj.size / SMALL_PAGE_SIZE;

	if (ADD_OVERFLOW(*idx, num_pages, &n) || n > tot_page_count)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!core_pbuf_is(CORE_MEM_NON_SEC, pa, num_pages * SMALL_PAGE_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	for (n = 0; n < num_pages; n++)
		ms->pages[n + *idx] = pa + n * SMALL_PAGE_SIZE;

	(*idx) += n;
	return TEE_SUCCESS;
}


uint32_t mobj_spci_mem_share(struct mobj_spci *ms)
{
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	SLIST_INSERT_HEAD(&shm_inactive_head, ms, link);
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	return ms->cookie;
}

struct mobj_spci *mobj_spci_mem_reclaim(uint32_t cookie)
{
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	ms = pop_from_list(&shm_inactive_head, cmp_cookie, cookie);
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	if (!ms)
		panic();

	return ms;
}

uint32_t mobj_spci_register_by_cookie(uint32_t cookie)
{
	struct mobj *mobj = NULL;

	mobj = mobj_spci_get_by_cookie(cookie);
	if (mobj) {
		struct mobj_spci *ms = to_mobj_spci(mobj);
		uint32_t exceptions = cpu_spin_lock_xsave(&shm_lock);

		if (ms->registered_by_cookie) {
			mobj = NULL;
		} else {
			assert(!ms->unregistered_by_cookie);
			ms->registered_by_cookie = true;
		}

		cpu_spin_unlock_xrestore(&shm_lock, exceptions);
	}

	if (!mobj)
		return TEE_ERROR_ITEM_NOT_FOUND;
	return TEE_SUCCESS;
}

static void unmap_helper(struct mobj_spci *ms)
{
	if (ms->mm) {
		core_mmu_unmap_pages(tee_mm_get_smem(ms->mm),
				     ms->mobj.size / SMALL_PAGE_SIZE);
		tee_mm_free(ms->mm);
		ms->mm = NULL;
	}
}

uint32_t mobj_spci_unregister_by_cookie(uint32_t cookie)
{
	uint32_t res = TEE_SUCCESS;
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;
	bool may_put = false;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	ms = find_in_list(&shm_head, cmp_cookie, cookie);
	if (ms && !ms->registered_by_cookie)
		ms = NULL;
	if (ms) {
		may_put = !ms->unregistered_by_cookie;
		ms->unregistered_by_cookie = true;
	}
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	if (!may_put) {
		if (ms) {
			panic();
			return TEE_ERROR_BUSY;
		} else
			return TEE_ERROR_ITEM_NOT_FOUND;
	}

	mobj_put(&ms->mobj);

	exceptions = cpu_spin_lock_xsave(&shm_lock);

	if (find_in_list(&shm_head, cmp_cookie, cookie))
		res = TEE_ERROR_BUSY;
	else
		res = TEE_SUCCESS;

	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	return res;
}

struct mobj *mobj_spci_get_by_cookie(uint32_t cookie)
{
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);

	ms = pop_from_list(&shm_inactive_head, cmp_cookie, cookie);
	if (ms) {
		assert(refcount_val(&ms->mobj.refc) == 0);
		assert(!ms->unregistered_by_cookie);
		refcount_set(&ms->mobj.refc, 1);
		SLIST_INSERT_HEAD(&shm_head, ms, link);
	} else {
		ms = find_in_list(&shm_head, cmp_cookie, cookie);
		if (ms)
			mobj_get(&ms->mobj);
	}


	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	if (!ms)
		return NULL;
	return &ms->mobj;
}

static TEE_Result mobj_spci_get_pa(struct mobj *mobj, size_t offset,
				   size_t granule, paddr_t *pa)
{
	struct mobj_spci *ms = to_mobj_spci(mobj);
	size_t full_offset = 0;
	paddr_t p = 0;

	if (!pa)
		return TEE_ERROR_GENERIC;

	full_offset = offset + ms->page_offset;
	if (full_offset >= mobj->size)
		return TEE_ERROR_GENERIC;

	switch (granule) {
	case 0:
		p = ms->pages[full_offset / SMALL_PAGE_SIZE] +
		    (full_offset & SMALL_PAGE_MASK);
		break;
	case SMALL_PAGE_SIZE:
		p = ms->pages[full_offset / SMALL_PAGE_SIZE];
		break;
	default:
		return TEE_ERROR_GENERIC;
	}
	*pa = p;

	return TEE_SUCCESS;
}
KEEP_PAGER(mobj_spci_get_pa);

static size_t mobj_spci_get_phys_offs(struct mobj *mobj,
				      size_t granule __maybe_unused)
{
	assert(granule >= mobj->phys_granule);

	return to_mobj_spci(mobj)->page_offset;
}

static void *mobj_spci_get_va(struct mobj *mobj, size_t offset)
{
	struct mobj_spci *ms = to_mobj_spci(mobj);

	if (!ms->mm)
		return NULL;

	return (void *)(tee_mm_get_smem(ms->mm) + offset + ms->page_offset);
}

static void mobj_spci_inactivate(struct mobj *mobj)
{
	struct mobj_spci *ms = to_mobj_spci(mobj);
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	if (!pop_from_list(&shm_head, cmp_ptr, (vaddr_t)ms))
		panic();
	unmap_helper(ms);
	SLIST_INSERT_HEAD(&shm_inactive_head, ms, link);
	ms->registered_by_cookie = false;
	ms->unregistered_by_cookie = false;
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);
}

static TEE_Result mobj_spci_get_cattr(struct mobj *mobj __unused,
				      uint32_t *cattr)
{
	if (!cattr)
		return TEE_ERROR_GENERIC;

	*cattr = TEE_MATTR_CACHE_CACHED;

	return TEE_SUCCESS;
}

static bool mobj_spci_matches(struct mobj *mobj __maybe_unused,
				   enum buf_is_attr attr)
{
	assert(mobj->ops == &mobj_spci_ops);

	return attr == CORE_MEM_NON_SEC || attr == CORE_MEM_REG_SHM;
}

static uint64_t mobj_spci_get_cookie(struct mobj *mobj)
{
	return to_mobj_spci(mobj)->cookie;
}

static const struct mobj_ops mobj_spci_ops __rodata_unpaged = {
	.get_pa = mobj_spci_get_pa,
	.get_phys_offs = mobj_spci_get_phys_offs,
	.get_va = mobj_spci_get_va,
	.get_cattr = mobj_spci_get_cattr,
	.matches = mobj_spci_matches,
	.free = mobj_spci_inactivate,
	.get_cookie = mobj_spci_get_cookie,
};





TEE_Result mobj_inc_map(struct mobj *mobj)
{
	TEE_Result res = TEE_SUCCESS;
	uint32_t exceptions = 0;
	struct mobj_spci *ms = to_mobj_spci_may_fail(mobj);

	if (!ms)
		return TEE_ERROR_GENERIC;

	if (refcount_inc(&ms->mapcount))
		return TEE_SUCCESS;

	exceptions = cpu_spin_lock_xsave(&shm_lock);

	if (refcount_val(&ms->mapcount))
		goto out;

	ms->mm = tee_mm_alloc(&tee_mm_shm, ms->mobj.size);
	if (!ms->mm) {
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto out;
	}

	res = core_mmu_map_pages(tee_mm_get_smem(ms->mm), ms->pages,
				 ms->mobj.size / SMALL_PAGE_SIZE,
				 MEM_AREA_NSEC_SHM);
	if (res) {
		tee_mm_free(ms->mm);
		ms->mm = NULL;
		goto out;
	}

	refcount_set(&ms->mapcount, 1);
out:
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	return res;
}

TEE_Result mobj_dec_map(struct mobj *mobj)
{
	struct mobj_spci *ms = to_mobj_spci_may_fail(mobj);
	uint32_t exceptions = 0;

	if (!ms)
		return TEE_ERROR_GENERIC;

	if (!refcount_dec(&ms->mapcount))
		return TEE_SUCCESS;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	unmap_helper(ms);
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	return TEE_SUCCESS;
}


static TEE_Result mobj_mapped_shm_init(void)
{
	vaddr_t pool_start = 0;
	vaddr_t pool_end = 0;

	core_mmu_get_mem_by_type(MEM_AREA_SHM_VASPACE, &pool_start, &pool_end);
	if (!pool_start || !pool_end)
		panic("Can't find region for shmem pool");

	if (!tee_mm_init(&tee_mm_shm, pool_start, pool_end, SMALL_PAGE_SHIFT,
		    TEE_MM_POOL_NO_FLAGS))
		panic("Could not create shmem pool");

	DMSG("Shared memory address range: %" PRIxVA ", %" PRIxVA,
	     pool_start, pool_end);
	return TEE_SUCCESS;
}

service_init(mobj_mapped_shm_init);
