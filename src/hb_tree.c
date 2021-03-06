/*
 * libdict -- height-balanced (AVL) tree implementation.
 *
 * Copyright (c) 2001-2014, Farooq Mela
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hb_tree.h"

#include "dict_private.h"
#include "tree_common.h"

typedef struct hb_node hb_node;

struct hb_node {
    TREE_NODE_FIELDS(hb_node);
    signed char		    bal;    /* TODO: store in unused low bits. */
};

struct hb_tree {
    TREE_FIELDS(hb_node);
};

struct hb_itor {
    TREE_ITERATOR_FIELDS(hb_tree, hb_node);
};

static dict_vtable hb_tree_vtable = {
    (dict_inew_func)	    hb_dict_itor_new,
    (dict_dfree_func)	    tree_free,
    (dict_insert_func)	    hb_tree_insert,
    (dict_search_func)	    tree_search,
    (dict_search_func)	    tree_search_le,
    (dict_search_func)	    tree_search_lt,
    (dict_search_func)	    tree_search_ge,
    (dict_search_func)	    tree_search_gt,
    (dict_remove_func)	    hb_tree_remove,
    (dict_clear_func)	    tree_clear,
    (dict_traverse_func)    tree_traverse,
    (dict_count_func)	    tree_count,
    (dict_verify_func)	    hb_tree_verify,
};

static itor_vtable hb_tree_itor_vtable = {
    (dict_ifree_func)	    tree_iterator_free,
    (dict_valid_func)	    tree_iterator_valid,
    (dict_invalidate_func)  tree_iterator_invalidate,
    (dict_next_func)	    tree_iterator_next,
    (dict_prev_func)	    tree_iterator_prev,
    (dict_nextn_func)	    tree_iterator_next_n,
    (dict_prevn_func)	    tree_iterator_prev_n,
    (dict_first_func)	    tree_iterator_first,
    (dict_last_func)	    tree_iterator_last,
    (dict_key_func)	    tree_iterator_key,
    (dict_datum_func)	    tree_iterator_datum,
    (dict_isearch_func)	    tree_iterator_search,
    (dict_isearch_func)	    tree_iterator_search_le,
    (dict_isearch_func)	    tree_iterator_search_lt,
    (dict_isearch_func)	    tree_iterator_search_ge,
    (dict_isearch_func)	    tree_iterator_search_gt,
    (dict_iremove_func)	    NULL,/* hb_itor_remove not implemented yet */
    (dict_icompare_func)    NULL,/* hb_itor_compare not implemented yet */
};

static bool	rot_left(hb_tree* tree, hb_node* node);
static bool	rot_right(hb_tree* tree, hb_node* node);
static size_t	node_height(const hb_node* node);
static size_t	node_mheight(const hb_node* node);
static size_t	node_pathlen(const hb_node* node, size_t level);
static hb_node*	node_new(void* key);

hb_tree*
hb_tree_new(dict_compare_func cmp_func)
{
    ASSERT(cmp_func != NULL);

    hb_tree* tree = MALLOC(sizeof(*tree));
    if (tree) {
	tree->root = NULL;
	tree->count = 0;
	tree->cmp_func = cmp_func;
	tree->rotation_count = 0;
    }
    return tree;
}

dict*
hb_dict_new(dict_compare_func cmp_func)
{
    dict* dct = MALLOC(sizeof(*dct));
    if (dct) {
	if (!(dct->_object = hb_tree_new(cmp_func))) {
	    FREE(dct);
	    return NULL;
	}
	dct->_vtable = &hb_tree_vtable;
    }
    return dct;
}

size_t
hb_tree_free(hb_tree* tree, dict_delete_func delete_func)
{
    ASSERT(tree != NULL);

    const size_t count = hb_tree_clear(tree, delete_func);
    FREE(tree);
    return count;
}

size_t
hb_tree_clear(hb_tree* tree, dict_delete_func delete_func)
{
    ASSERT(tree != NULL);

    hb_node* node = tree->root;
    const size_t count = tree->count;
    while (node) {
	if (node->llink) {
	    node = node->llink;
	    continue;
	}
	if (node->rlink) {
	    node = node->rlink;
	    continue;
	}

	if (delete_func)
	    delete_func(node->key, node->datum);

	hb_node* parent = node->parent;
	FREE(node);
	tree->count--;

	if (parent) {
	    if (parent->llink == node)
		parent->llink = NULL;
	    else
		parent->rlink = NULL;
	}
	node = parent;
    }

    tree->root = NULL;
    ASSERT(tree->count == 0);

    return count;
}

void**
hb_tree_search(hb_tree* tree, const void* key)
{
    return tree_search(tree, key);
}

dict_insert_result
hb_tree_insert(hb_tree* tree, void* key)
{
    ASSERT(tree != NULL);

    int cmp = 0;
    hb_node* node = tree->root;
    hb_node* parent = NULL;
    hb_node* q = NULL;
    while (node) {
	cmp = tree->cmp_func(key, node->key);
	if (cmp < 0)
	    parent = node, node = node->llink;
	else if (cmp)
	    parent = node, node = node->rlink;
	else
	    return (dict_insert_result) { &node->datum, false };

	if (parent->bal)
	    q = parent;
    }

    hb_node* const add = node = node_new(key);
    if (!node)
	return (dict_insert_result) { NULL, false };

    if (!(node->parent = parent)) {
	ASSERT(tree->count == 0);
	ASSERT(tree->root == NULL);
	tree->root = node;
    } else {
	if (cmp < 0)
	    parent->llink = node;
	else
	    parent->rlink = node;

	while (parent != q) {
	    ASSERT(parent->bal == 0);
	    parent->bal = (parent->rlink == node) ? 1 : -1;
	    node = parent;
	    parent = node->parent;
	}
	if (q) {
	    if (q->llink == node) {
		if (--q->bal == -2) {
		    if (q->llink->bal > 0) {
			/* LR: rotate q->llink left, then rotate q right. */
			hb_node* const ql = q->llink;
			hb_node* const qlr = ql->rlink;

			hb_node *const qp = q->parent;
			*(qp == NULL ? &tree->root : qp->llink == q ? &qp->llink : &qp->rlink) = qlr;
			qlr->parent = qp;

			if ((q->llink = qlr->rlink) != NULL)
			    q->llink->parent = q;
			if ((ql->rlink = qlr->llink) != NULL)
			    ql->rlink->parent = ql;
			qlr->llink = ql;
			qlr->rlink = q;
			ql->parent = q->parent = qlr;
			q->bal = (qlr->bal == -1);
			ql->bal = -(qlr->bal == 1);
			qlr->bal = 0;

			tree->rotation_count += 2;
		    } else {
			/* R: rotate q right. */
			hb_node* const ql = q->llink;

			hb_node *const qp = q->parent;
			*(qp == NULL ? &tree->root : qp->llink == q ? &qp->llink : &qp->rlink) = ql;
			ql->parent = qp;

			if ((q->llink = ql->rlink) != NULL)
			    q->llink->parent = q;
			ql->rlink = q;
			q->parent = ql;
			q->bal = ql->bal = 0;

			tree->rotation_count += 1;
		    }
		}
	    } else {
		ASSERT(q->rlink == node);
		if (++q->bal == +2) {
		    if (q->rlink->bal < 0) {
			/* Rotate q->rlink right, then q left. */
			hb_node* const qr = q->rlink;
			hb_node* const qrl = qr->llink;

			hb_node *const qp = q->parent;
			*(qp == NULL ? &tree->root : qp->llink == q ? &qp->llink : &qp->rlink) = qrl;
			qrl->parent = qp;

			if ((q->rlink = qrl->llink) != NULL)
			    q->rlink->parent = q;
			if ((qr->llink = qrl->rlink) != NULL)
			    qr->llink->parent = qr;
			qrl->llink = q;
			qrl->rlink = qr;
			qr->parent = q->parent = qrl;
			q->bal = -(qrl->bal == 1);
			qr->bal = (qrl->bal == -1);
			qrl->bal = 0;

			tree->rotation_count += 2;
		    } else {
			/* R: rotate q left */
			hb_node* const qr = q->rlink;

			hb_node *const qp = q->parent;
			*(qp == NULL ? &tree->root : qp->llink == q ? &qp->llink : &qp->rlink) = qr;
			qr->parent = qp;

			if ((q->rlink = qr->llink) != NULL)
			    q->rlink->parent = q;
			qr->llink = q;
			q->parent = qr;
			q->bal = qr->bal = 0;

			tree->rotation_count += 1;
		    }
		}
	    }
	}
    }
    tree->count++;
    return (dict_insert_result) { &add->datum, true };
}

dict_remove_result
hb_tree_remove(hb_tree* tree, const void* key)
{
    ASSERT(tree != NULL);

    hb_node* node = tree->root;
    hb_node* parent = NULL;
    while (node) {
	int cmp = tree->cmp_func(key, node->key);
	if (cmp < 0)
	    parent = node, node = node->llink;
	else if (cmp)
	    parent = node, node = node->rlink;
	else
	    break;
    }
    if (!node)
	return (dict_remove_result) { NULL, NULL, false };

    if (node->llink && node->rlink) {
	hb_node* out;
	if (node->bal > 0) {
	    out = node->rlink;
	    while (out->llink)
		out = out->llink;
	} else {
	    out = node->llink;
	    while (out->rlink)
		out = out->rlink;
	}
	void* tmp;
	SWAP(node->key, out->key, tmp);
	SWAP(node->datum, out->datum, tmp);
	node = out;
	parent = out->parent;
    }

    dict_remove_result result = { node->key, node->datum, true };
    hb_node* child = node->llink ? node->llink : node->rlink;
    FREE(node);
    if (child)
	child->parent = parent;
    if (!parent) {
	tree->root = child;
	tree->count--;
	return result;
    }

    bool left = parent->llink == node;
    if (left)
	parent->llink = child;
    else
	parent->rlink = child;

    unsigned rotations = 0;
    for (;;) {
	if (left) {
	    if (++parent->bal == 0) {
		node = parent;
		goto higher;
	    }
	    if (parent->bal == +2) {
		ASSERT(parent->rlink != NULL);
		if (parent->rlink->bal < 0) {
		    rotations += 2;
		    rot_right(tree, parent->rlink);
		    rot_left(tree, parent);
		} else {
		    rotations += 1;
		    ASSERT(parent->rlink->rlink != NULL);
		    if (!rot_left(tree, parent))
			break;
		}
	    } else {
		break;
	    }
	} else {
	    if (--parent->bal == 0) {
		node = parent;
		goto higher;
	    }
	    if (parent->bal == -2) {
		ASSERT(parent->llink != NULL);
		if (parent->llink->bal > 0) {
		    rotations += 2;
		    rot_left(tree, parent->llink);
		    rot_right(tree, parent);
		} else {
		    rotations += 1;
		    ASSERT(parent->llink->llink != NULL);
		    if (!rot_right(tree, parent))
			break;
		}
	    } else {
		break;
	    }
	}

	/* Only get here on double rotations or single rotations that changed
	 * subtree height - in either event, `parent->parent' is positioned
	 * where `parent' was positioned before any rotations. */
	node = parent->parent;
higher:
	if (!(parent = node->parent))
	    break;
	left = (parent->llink == node);
    }
    tree->rotation_count += rotations;
    tree->count--;
    return result;
}

const void*
hb_tree_min(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    const hb_node* node = tree->root;
    if (!node)
	return NULL;
    for (; node->llink; node = node->llink)
	/* void */;
    return node->key;
}

const void*
hb_tree_max(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    const hb_node* node = tree->root;
    if (!node)
	return NULL;
    for (; node->rlink; node = node->rlink)
	/* void */;
    return node->key;
}

size_t
hb_tree_traverse(hb_tree* tree, dict_visit_func visit)
{
    ASSERT(tree != NULL);

    return tree_traverse(tree, visit);
}

size_t
hb_tree_count(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    return tree_count(tree);
}

size_t
hb_tree_height(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    return tree->root ? node_height(tree->root) : 0;
}

size_t
hb_tree_mheight(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    return tree->root ? node_mheight(tree->root) : 0;
}

size_t
hb_tree_pathlen(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    return tree->root ? node_pathlen(tree->root, 1) : 0;
}

static hb_node*
node_new(void* key)
{
    hb_node* node = MALLOC(sizeof(*node));
    if (node) {
	node->key = key;
	node->datum = NULL;
	node->parent = NULL;
	node->llink = NULL;
	node->rlink = NULL;
	node->bal = 0;
    }
    return node;
}

static size_t
node_height(const hb_node* node)
{
    ASSERT(node != NULL);

    size_t l = node->llink ? node_height(node->llink) + 1 : 0;
    size_t r = node->rlink ? node_height(node->rlink) + 1 : 0;
    return MAX(l, r);
}

static size_t
node_mheight(const hb_node* node)
{
    ASSERT(node != NULL);

    size_t l = node->llink ? node_mheight(node->llink) + 1 : 0;
    size_t r = node->rlink ? node_mheight(node->rlink) + 1 : 0;
    return MIN(l, r);
}

static size_t
node_pathlen(const hb_node* node, size_t level)
{
    ASSERT(node != NULL);

    size_t n = 0;
    if (node->llink)
	n += level + node_pathlen(node->llink, level + 1);
    if (node->rlink)
	n += level + node_pathlen(node->rlink, level + 1);
    return n;
}

/*
 * rot_left(T, B):
 *
 *     /	     /
 *    B	     D
 *   / \	   / \
 *  A   D   ==>   B   E
 *     / \       / \
 *    C   E     A   C
 *
 */
static bool
rot_left(hb_tree* tree, hb_node* node)
{
    ASSERT(tree != NULL);
    ASSERT(node != NULL);
    ASSERT(node->rlink != NULL);

    hb_node* rlink = node->rlink;
    tree_node_rot_left(tree, node);

    bool height_changed = (rlink->bal != 0);
    node->bal  -= 1 + MAX(rlink->bal, 0);
    rlink->bal -= 1 - MIN(node->bal, 0);
    return height_changed;
}

/*
 * rot_right(T, D):
 *
 *       /	   /
 *      D	   B
 *     / \	 / \
 *    B   E  ==>  A   D
 *   / \	     / \
 *  A   C	   C   E
 *
 */
static bool
rot_right(hb_tree* tree, hb_node* node)
{
    ASSERT(tree != NULL);
    ASSERT(node != NULL);
    ASSERT(node->llink != NULL);

    hb_node* llink = node->llink;
    tree_node_rot_right(tree, node);

    bool height_changed = (llink->bal != 0);
    node->bal  += 1 - MIN(llink->bal, 0);
    llink->bal += 1 + MAX(node->bal, 0);
    return height_changed;
}

static bool
node_verify(const hb_tree* tree, const hb_node* parent, const hb_node* node,
	    unsigned* height)
{
    ASSERT(tree != NULL);

    if (!parent) {
	VERIFY(tree->root == node);
    } else {
	VERIFY(parent->llink == node || parent->rlink == node);
    }
    if (node) {
	VERIFY(node->parent == parent);
	VERIFY(node->bal >= -1);
	VERIFY(node->bal <= 1);
	unsigned lheight, rheight;
	if (!node_verify(tree, node, node->llink, &lheight) ||
	    !node_verify(tree, node, node->rlink, &rheight))
	    return false;
	VERIFY(node->bal == (int)rheight - (int)lheight);
	if (height)
	    *height = MAX(lheight, rheight) + 1;
    } else {
	if (height)
	    *height = 0;
    }
    return true;
}

bool
hb_tree_verify(const hb_tree* tree)
{
    ASSERT(tree != NULL);

    if (tree->root) {
	VERIFY(tree->count > 0);
    } else {
	VERIFY(tree->count == 0);
    }
    return node_verify(tree, NULL, tree->root, NULL);
}

hb_itor*
hb_itor_new(hb_tree* tree)
{
    ASSERT(tree != NULL);

    hb_itor* itor = MALLOC(sizeof(*itor));
    if (itor) {
	itor->tree = tree;
	itor->node = NULL;
    }
    return itor;
}

dict_itor*
hb_dict_itor_new(hb_tree* tree)
{
    ASSERT(tree != NULL);

    dict_itor* itor = MALLOC(sizeof(*itor));
    if (itor) {
	if (!(itor->_itor = hb_itor_new(tree))) {
	    FREE(itor);
	    return NULL;
	}
	itor->_vtable = &hb_tree_itor_vtable;
    }
    return itor;
}

void
hb_itor_free(hb_itor* itor)
{
    ASSERT(itor != NULL);

    FREE(itor);
}

bool
hb_itor_valid(const hb_itor* itor)
{
    ASSERT(itor != NULL);

    return itor->node != NULL;
}

void
hb_itor_invalidate(hb_itor* itor)
{
    ASSERT(itor != NULL);

    itor->node = NULL;
}

bool
hb_itor_next(hb_itor* itor)
{
    ASSERT(itor != NULL);

    if (!itor->node)
	hb_itor_first(itor);
    else
	itor->node = tree_node_next(itor->node);
    return itor->node != NULL;
}

bool
hb_itor_prev(hb_itor* itor)
{
    ASSERT(itor != NULL);

    if (!itor->node)
	hb_itor_last(itor);
    else
	itor->node = tree_node_prev(itor->node);
    return itor->node != NULL;
}

bool
hb_itor_nextn(hb_itor* itor, size_t count)
{
    ASSERT(itor != NULL);

    while (count--)
	if (!hb_itor_prev(itor))
	    return false;
    return itor->node != NULL;
}

bool
hb_itor_prevn(hb_itor* itor, size_t count)
{
    ASSERT(itor != NULL);

    while (count--)
	if (!hb_itor_prev(itor))
	    return false;
    return itor->node != NULL;
}

bool
hb_itor_first(hb_itor* itor)
{
    ASSERT(itor != NULL);

    itor->node = itor->tree->root ? tree_node_min(itor->tree->root) : NULL;
    return itor->node != NULL;
}

bool
hb_itor_last(hb_itor* itor)
{
    ASSERT(itor != NULL);

    itor->node = itor->tree->root ? tree_node_max(itor->tree->root) : NULL;
    return itor->node != NULL;
}

const void*
hb_itor_key(const hb_itor* itor)
{
    ASSERT(itor != NULL);

    return itor->node ? itor->node->key : NULL;
}

void**
hb_itor_datum(hb_itor* itor)
{
    ASSERT(itor != NULL);

    return itor->node ? &itor->node->datum : NULL;
}
