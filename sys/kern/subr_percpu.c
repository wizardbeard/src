/*	$OpenBSD: subr_percpu.c,v 1.3 2016/10/24 03:15:38 deraadt Exp $ */

/*
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pool.h>
#include <sys/malloc.h>
#include <sys/types.h>

#include <sys/percpu.h>

#ifdef MULTIPROCESSOR
struct pool cpumem_pl;

void
percpu_init(void)
{
	pool_init(&cpumem_pl, sizeof(struct cpumem) * ncpus, 0, IPL_NONE,
	    PR_WAITOK, "percpumem", &pool_allocator_single);
}

struct cpumem *
cpumem_get(struct pool *pp)
{
	struct cpumem *cm;
	unsigned int cpu;

	cm = pool_get(&cpumem_pl, PR_WAITOK);

	for (cpu = 0; cpu < ncpus; cpu++)
		cm[cpu].mem = pool_get(pp, PR_WAITOK | PR_ZERO);

	return (cm);
}

void
cpumem_put(struct pool *pp, struct cpumem *cm)
{
	unsigned int cpu;

	for (cpu = 0; cpu < ncpus; cpu++)
		pool_put(pp, cm[cpu].mem);

	pool_put(&cpumem_pl, cm);
}

struct cpumem *
cpumem_malloc(size_t sz, int type)
{
	struct cpumem *cm;
	unsigned int cpu;

	sz = roundup(sz, CACHELINESIZE);

	cm = pool_get(&cpumem_pl, PR_WAITOK);

	for (cpu = 0; cpu < ncpus; cpu++)
		cm[cpu].mem = malloc(sz, type, M_WAITOK | M_ZERO);

	return (cm);
}

struct cpumem *
cpumem_realloc(struct cpumem *bootcm, size_t sz, int type)
{
	struct cpumem *cm;
	unsigned int cpu;

	sz = roundup(sz, CACHELINESIZE);

	cm = pool_get(&cpumem_pl, PR_WAITOK);

	cm[0].mem = bootcm[0].mem;
	for (cpu = 1; cpu < ncpus; cpu++)
		cm[cpu].mem = malloc(sz, type, M_WAITOK | M_ZERO);

	return (cm);
}

void
cpumem_free(struct cpumem *cm, int type, size_t sz)
{
	unsigned int cpu;

	sz = roundup(sz, CACHELINESIZE);

	for (cpu = 0; cpu < ncpus; cpu++)
		free(cm[cpu].mem, type, sz);

	pool_put(&cpumem_pl, cm);
}

void *
cpumem_first(struct cpumem_iter *i, struct cpumem *cm)
{
	i->cpu = 0;

	return (cm[0].mem);
}

void *
cpumem_next(struct cpumem_iter *i, struct cpumem *cm)
{
	unsigned int cpu = ++i->cpu;

	if (cpu >= ncpus)
		return (NULL);

	return (cm[cpu].mem);
}

struct cpumem *
counters_alloc(unsigned int n, int type)
{
	struct cpumem *cm;
	struct cpumem_iter cmi;
	uint64_t *counters;
	unsigned int i;

	KASSERT(n > 0);

	n++; /* add space for a generation number */
	cm = cpumem_malloc(n * sizeof(uint64_t), type);

	CPUMEM_FOREACH(counters, &cmi, cm) {
		for (i = 0; i < n; i++)
			counters[i] = 0;
	}

	return (cm);
}

struct cpumem *
counters_realloc(struct cpumem *cm, unsigned int n, int type)
{
	n++; /* the generation number */
	return (cpumem_realloc(cm, n * sizeof(uint64_t), type));
}

void
counters_free(struct cpumem *cm, int type, unsigned int n)
{
	n++; /* generation number */
	cpumem_free(cm, type, n * sizeof(uint64_t));
}

void
counters_read(struct cpumem *cm, uint64_t *output, unsigned int n)
{
	struct cpumem_iter cmi;
	uint64_t *gen, *counters, *temp;
	uint64_t enter, leave;
	unsigned int i;

	for (i = 0; i < n; i++)
		output[i] = 0;

	temp = mallocarray(n, sizeof(uint64_t), M_TEMP, M_WAITOK);

	gen = cpumem_first(&cmi, cm);
	do {
		counters = gen + 1;

		enter = *gen;
		for (;;) {
			/* the generation number is odd during an update */
			while (enter & 1) {
				yield();
				membar_consumer();
				enter = *gen;
			}

			for (i = 0; i < n; i++)
				temp[i] = counters[i];

			membar_consumer();
			leave = *gen;

			if (enter == leave)
				break;

			enter = leave;
		}

		for (i = 0; i < n; i++)
			output[i] += temp[i];

		gen = cpumem_next(&cmi, cm);
	} while (gen != NULL);

	free(temp, M_TEMP, n * sizeof(uint64_t));
}

void
counters_zero(struct cpumem *cm, unsigned int n)
{
	struct cpumem_iter cmi;
	uint64_t *counters;
	unsigned int i;

	n++; /* zero the generation numbers too */

	counters = cpumem_first(&cmi, cm);
	do {
		for (i = 0; i < n; i++)
			counters[i] = 0;

		counters = cpumem_next(&cmi, cm);
	} while (counters != NULL);
}

#else /* MULTIPROCESSOR */

/*
 * Uniprocessor implementation of per-CPU data structures.
 *
 * UP percpu memory is a single memory allocation cast to/from the
 * cpumem struct. It is not scaled up to the size of cacheline because
 * there's no other cache to contend with.
 */

void
percpu_init(void)
{
	/* nop */
}

struct cpumem *
cpumem_get(struct pool *pp)
{
	return (pool_get(pp, PR_WAITOK | PR_ZERO));
}

void
cpumem_put(struct pool *pp, struct cpumem *cm)
{
	pool_put(pp, cm);
}

struct cpumem *
cpumem_malloc(size_t sz, int type)
{
	return (malloc(sz, type, M_WAITOK | M_ZERO));
}

struct cpumem *
cpumem_realloc(struct cpumem *cm, size_t sz, int type)
{
	return (cm);
}

void
cpumem_free(struct cpumem *cm, int type, size_t sz)
{
	free(cm, type, sz);
}

void *
cpumem_first(struct cpumem_iter *i, struct cpumem *cm)
{
	return (cm);
}

void *
cpumem_next(struct cpumem_iter *i, struct cpumem *cm)
{
	return (NULL);
}

struct cpumem *
counters_alloc(unsigned int n, int type)
{
	KASSERT(n > 0);

	return (cpumem_malloc(n * sizeof(uint64_t), type));
}

struct cpumem *
counters_realloc(struct cpumem *cm, unsigned int n, int type)
{
	/* this is unecessary, but symmetrical */
	return (cpumem_realloc(cm, n * sizeof(uint64_t), type));
}

void
counters_free(struct cpumem *cm, int type, unsigned int n)
{
	cpumem_free(cm, type, n * sizeof(uint64_t));
}

void
counters_read(struct cpumem *cm, uint64_t *output, unsigned int n)
{
	uint64_t *counters;
	unsigned int i;
	int s;

	counters = (uint64_t *)cm;

	s = splhigh();
	for (i = 0; i < n; i++)
		output[i] = counters[i];
	splx(s);
}

void
counters_zero(struct cpumem *cm, unsigned int n)
{
	uint64_t *counters;
	unsigned int i;
	int s;

	counters = (uint64_t *)cm;

	s = splhigh();
	for (i = 0; i < n; i++)
		counters[i] = 0;
	splx(s);
}

#endif /* MULTIPROCESSOR */

