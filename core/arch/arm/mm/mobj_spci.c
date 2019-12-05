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

struct get_by_cookie_param {
	uint32_t cookie;
	unsigned int page_count;
};

SLIST_HEAD(mobj_spci_head, mobj_spci);

#ifdef CFG_CORE_SEL1_SPMC
#define NUM_SHMS 64
static bitstr_t bit_decl(shm_bits, NUM_SHMS);
#endif

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

static struct mobj_spci *mobj_spci_new(unsigned int num_pages)
{
	struct mobj_spci *ms = NULL;
	size_t s = 0;

	if (!num_pages)
		return NULL;

	s = shm_size(num_pages);
	if (!s)
		return NULL;
	ms = calloc(1, s);
	if (!ms)
		return NULL;

	ms->mobj.ops = &mobj_spci_ops;
	ms->mobj.size = num_pages * SMALL_PAGE_SIZE;
	ms->mobj.phys_granule = SMALL_PAGE_SIZE;
	refcount_set(&ms->mobj.refc, 0);

	return ms;
}

#ifdef CFG_CORE_SEL1_SPMC
struct mobj_spci *mobj_spci_sel1_spmc_new(unsigned int num_pages)
{
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;
	int i = 0;

	ms = mobj_spci_new(num_pages);
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

	return ms;
}
#endif /*CFG_CORE_SEL1_SPMC*/

static size_t get_page_count(struct mobj_spci *ms)
{
	return ROUNDUP(ms->mobj.size, SMALL_PAGE_SIZE) / SMALL_PAGE_SIZE;
}

static bool cmp_cookie(struct mobj_spci *ms, vaddr_t cookie)
{
	return ms->cookie == cookie;
}

static bool cmp_get_by_cookie_param(struct mobj_spci *ms,
				    vaddr_t get_by_cookie_param)
{
	struct get_by_cookie_param *arg = (void *)get_by_cookie_param;

	return ms->cookie == arg->cookie &&
	       get_page_count(ms) == arg->page_count;
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

#ifdef CFG_CORE_SEL1_SPMC
void mobj_spci_sel1_spmc_delete(struct mobj_spci *ms)
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
#endif /*CFG_CORE_SEL1_SPMC*/

uint32_t mobj_spci_add_pages_at(struct mobj_spci *ms, unsigned int *idx,
				paddr_t pa, unsigned int num_pages)
{
	unsigned int n;
	size_t tot_page_count = get_page_count(ms);

	if (ADD_OVERFLOW(*idx, num_pages, &n) || n > tot_page_count)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!core_pbuf_is(CORE_MEM_NON_SEC, pa, num_pages * SMALL_PAGE_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	for (n = 0; n < num_pages; n++)
		ms->pages[n + *idx] = pa + n * SMALL_PAGE_SIZE;

	(*idx) += n;
	return TEE_SUCCESS;
}

uint32_t mobj_spci_push_to_inactive(struct mobj_spci *ms)
{
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	assert(!find_in_list(&shm_inactive_head, cmp_ptr, (vaddr_t)ms));
	assert(!find_in_list(&shm_inactive_head, cmp_cookie, ms->cookie));
	assert(!find_in_list(&shm_head, cmp_cookie, ms->cookie));
	SLIST_INSERT_HEAD(&shm_inactive_head, ms, link);
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	return ms->cookie;
}

static void unmap_helper(struct mobj_spci *ms)
{
	if (ms->mm) {
		core_mmu_unmap_pages(tee_mm_get_smem(ms->mm),
				     get_page_count(ms));
		tee_mm_free(ms->mm);
		ms->mm = NULL;
	}
}

uint32_t mobj_spci_unregister_by_cookie(uint32_t cookie)
{
	uint32_t res = TEE_SUCCESS;
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	ms = find_in_list(&shm_head, cmp_cookie, cookie);
	/*
	 * If the mobj is found here it's still active and cannot be
	 * unregistered.
	 */
	if (ms) {
		DMSG("cookie %#"PRIx32" busy refc %u",
		      cookie, refcount_val(&ms->mobj.refc));
		res = TEE_ERROR_BUSY;
		goto out;
	}
	ms = find_in_list(&shm_inactive_head, cmp_cookie, cookie);
	/*
	 * If the mobj isn't found or if it already have been unregistered.
	 */
	if (!ms || ms->unregistered_by_cookie) {
		res = TEE_ERROR_ITEM_NOT_FOUND;
		goto out;
	}
	ms->unregistered_by_cookie = true;
	res = TEE_SUCCESS;

out:
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);
	return res;
}

#ifdef CFG_CORE_SEL1_SPMC
uint32_t mobj_spci_sel1_spmc_reclaim(uint32_t cookie)
{
	uint32_t res = TEE_SUCCESS;
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;

	exceptions = cpu_spin_lock_xsave(&shm_lock);
	ms = find_in_list(&shm_head, cmp_cookie, cookie);
	/*
	 * If the mobj is found here it's still active and cannot be
	 * reclaimed.
	 */
	if (ms) {
		DMSG("cookie %#"PRIx32" busy refc %u",
		      cookie, refcount_val(&ms->mobj.refc));
		res = TEE_ERROR_BUSY;
		goto out;
	}

	ms = find_in_list(&shm_inactive_head, cmp_cookie, cookie);
	/* If the mobj isn't found */
	if (!ms) {
		res = TEE_ERROR_ITEM_NOT_FOUND;
		goto out;
	}
	/*
	 * If the mobj has been registered via mobj_spci_get_by_cookie()
	 * but not unregistered yet with mobj_spci_unregister_by_cookie().
	 */
	if (ms->registered_by_cookie && !ms->unregistered_by_cookie) {
		DMSG("cookie %#"PRIx32" busy", cookie);
		res = TEE_ERROR_BUSY;
		goto out;
	}

	if (!pop_from_list(&shm_inactive_head, cmp_ptr, (vaddr_t)ms))
		panic();
	res = TEE_SUCCESS;
out:
	cpu_spin_unlock_xrestore(&shm_lock, exceptions);
	if (!res)
		mobj_spci_sel1_spmc_delete(ms);
	return res;
}
#endif /*CFG_CORE_SEL1_SPMC*/

struct mobj *mobj_spci_get_by_cookie(uint32_t cookie,
				     unsigned int internal_offs,
				     unsigned int page_count)
{
	struct mobj_spci *ms = NULL;
	uint32_t exceptions = 0;

	if (internal_offs >= SMALL_PAGE_SIZE)
		return NULL;

	exceptions = cpu_spin_lock_xsave(&shm_lock);

	ms = find_in_list(&shm_head, cmp_cookie, cookie);
	if (ms) {
		mobj_get(&ms->mobj);
		DMSG("cookie %#"PRIx32" active: refc %d",
		     cookie, refcount_val(&ms->mobj.refc));
	} else {
		struct get_by_cookie_param arg = {
			.cookie = cookie,
			.page_count = page_count,
		};
		ms = pop_from_list(&shm_inactive_head, cmp_get_by_cookie_param,
				   (vaddr_t)&arg);
		if (ms) {
			ms->unregistered_by_cookie = false;
			ms->registered_by_cookie = true;
			assert(refcount_val(&ms->mobj.refc) == 0);
			refcount_set(&ms->mobj.refc, 1);
			refcount_set(&ms->mapcount, 0);
			ms->mobj.size += ms->page_offset;
			assert(!(ms->mobj.size & SMALL_PAGE_MASK));
			ms->mobj.size -= internal_offs;
			ms->page_offset = internal_offs;
			SLIST_INSERT_HEAD(&shm_head, ms, link);
		}
	}

	cpu_spin_unlock_xrestore(&shm_lock, exceptions);

	if (!ms) {
		EMSG("Failed to get cookie %#"PRIx32" page_count %#x",
		     cookie, page_count);
		return NULL;
	}

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

	if (offset >= mobj->size)
		return TEE_ERROR_GENERIC;

	full_offset = offset + ms->page_offset;
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

	if (!ms->mm || offset >= mobj->size)
		return NULL;

	return (void *)(tee_mm_get_smem(ms->mm) + offset + ms->page_offset);
}

static void mobj_spci_inactivate(struct mobj *mobj)
{
	struct mobj_spci *ms = to_mobj_spci(mobj);
	uint32_t exceptions = 0;

	EMSG("cookie %#"PRIx32, ms->cookie);
	exceptions = cpu_spin_lock_xsave(&shm_lock);
	if (!pop_from_list(&shm_head, cmp_ptr, (vaddr_t)ms))
		panic();
	unmap_helper(ms);
	SLIST_INSERT_HEAD(&shm_inactive_head, ms, link);
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
				 get_page_count(ms), MEM_AREA_NSEC_SHM);
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
