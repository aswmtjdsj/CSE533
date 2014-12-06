#pragma once

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

#define talloc(nmem, type) calloc(nmem, sizeof(type))

#define MAX_HEIGHT 32

struct skip_list_head {
	struct skip_list_head **next, **prev;
	int h;
};

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define skip_list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define skip_list_foreach(head, tmp, ptr, type, member) \
	for ((tmp) = (head)->next[0]; \
	     (ptr) = skip_list_entry(tmp, type, member), (tmp); \
	     (tmp) = (tmp)->next[0])

#define skip_list_foreach_safe(head, tmp, tmp2, ptr, type, member) \
	for ((tmp) = (head)->next[0]; \
	     (ptr) = skip_list_entry(tmp, type, member), \
	     (tmp2) = (tmp)->next[0], \
	     (tmp); (tmp) = (tmp2))

#define skip_list_empty(ptr) \
	((ptr)->next == NULL || (ptr)->next[0] == NULL)

static inline void skip_list_init_head(struct skip_list_head *h){
	h->h = MAX_HEIGHT;
	h->next = calloc(MAX_HEIGHT, sizeof(void *));
	h->prev = NULL;
}

static inline void skip_list_deinit_head(struct skip_list_head *h){
	h->h = 0;
	free(h->next);
	h->next = NULL;
}

static inline int skip_list_gen_height(void){
	int r = random();
	int h = 1;
	for(;r&1;r>>=1)
		h++;
	return h;
}

static inline void skip_list_init_node(struct skip_list_head *n){
	int newh = skip_list_gen_height();
	n->next = talloc(newh, struct skip_list_head *);
	n->prev = talloc(newh, struct skip_list_head *);
	n->h = newh;
}

static inline void skip_list_deinit_node(struct skip_list_head *n){
	free(n->next);
	free(n->prev);
	n->next = n->prev = NULL;
	n->h = 0;
}

typedef int (*skip_list_cmp)(struct skip_list_head *a, const void *key);

static int skip_list_last_cmp(struct skip_list_head *a, void *key){
	return a ? -1 : 1;
}

static inline void skip_list_previous(struct skip_list_head *head,
				      void *key, skip_list_cmp cmp,
				      struct skip_list_head **res){
	int h = head->h-1;
	struct skip_list_head *n = head;
	while(1){
		while(h >= 0 &&
		     (n->next[h] == NULL || cmp(n->next[h], key) >= 0)){
			res[h] = n;
			h--;
		}
		if (h < 0)
			break;
		while(n->next[h] && cmp(n->next[h], key) < 0)
			n = n->next[h];
	}
}

static inline void
skip_list_insert(struct skip_list_head *h, struct skip_list_head *n,
		      void *key, skip_list_cmp cmp) {
	skip_list_init_node(n);
	int i;
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	for(i = 0; i < n->h; i++) {
		n->next[i] = hs[i]->next[i];
		hs[i]->next[i] = n;
		n->prev[i] = hs[i];
	}
	for(i = 0; i < n->h; i++) {
		if (!n->next[i])
			break;
		n->next[i]->prev[i] = n;
	}
}

//Find the smallest element that is greater than or equal to key.
static inline struct skip_list_head *
skip_list_find_ge(struct skip_list_head *h, void *key, skip_list_cmp cmp){
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	return hs[0]->next[0];
}

//Find the smallest element that is greater than or equal to key.
static inline struct skip_list_head *
skip_list_find_eq(struct skip_list_head *h, void *key, skip_list_cmp cmp){
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	if (hs[0]->next[0] && cmp(hs[0]->next[0], key) == 0)
		return hs[0]->next[0];
	return NULL;
}

//Find the smallest element that is less than or equal to key.
static inline struct skip_list_head *
skip_list_find_le(struct skip_list_head *h, void *key, skip_list_cmp cmp){
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	if (hs[0]->next[0] && cmp(hs[0]->next[0], key) == 0)
		return hs[0]->next[0];
	return hs[0] == h ? NULL : hs[0];
}

static inline struct skip_list_head *
skip_list_extract_by_key(struct skip_list_head *h, void *key, skip_list_cmp cmp){
	int i;
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	struct skip_list_head *node = hs[0]->next[0];
	if (!node || cmp(node, key) != 0)
		return NULL;
	for(i = 0; i < node->h; i++) {
		assert(hs[i] == node->prev[i]);
		hs[i]->next[i] = node->next[i];
		if (node->next[i])
			node->next[i]->prev[i] = node->prev[i];
	}
	skip_list_deinit_node(node);
	return node;
}


static inline void
skip_list_delete(struct skip_list_head *h){
	int i;
	for(i = 0; i < h->h; i++) {
		h->prev[i]->next[i] = h->next[i];
		if (h->next[i])
			h->next[i]->prev[i] = h->prev[i];
	}
	skip_list_deinit_node(h);
}

static inline void
skip_list_clear(struct skip_list_head *h, void (*freep)(struct skip_list_head *)){
	struct skip_list_head *hx = h->next[0];
	while(hx){
		struct skip_list_head *tmp = hx->next[0];
		freep(hx);
		hx = tmp;
	}
}

