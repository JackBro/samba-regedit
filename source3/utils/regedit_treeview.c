/*
 * Samba Unix/Linux SMB client library
 * Registry Editor
 * Copyright (C) Christopher Davis 2012
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "regedit_treeview.h"

struct tree_node *tree_node_new(TALLOC_CTX *ctx, struct tree_node *parent, const char *name)
{
	struct tree_node *node;

	node = talloc_zero(ctx, struct tree_node);
	if (!node) {
		return NULL;
	}

	node->name = talloc_strdup(ctx, name);
	if (!node->name) {
		return NULL;
	}

	if (parent) {
		/* Check if this node is the first descendant of parent. */
		if (!parent->child_head) {
			parent->child_head = node;
		}
		node->parent = parent;
	}

	return node;
}

void tree_node_append(struct tree_node *left, struct tree_node *right)
{
	if (left->next) {
		right->next = left->next;
		left->next->previous = right;
	}	
	left->next = right;
	right->previous = left;
}

struct tree_node *tree_node_pop(struct tree_node **plist)
{
	struct tree_node *node;

	node = *plist;

	if (node == NULL)
		return NULL;

	*plist = node->previous;
	if (*plist == NULL)
		*plist = node->next;
	if (node->previous) {
		node->previous->next = node->next;
	}
	if (node->next) {
		node->next->previous = node->previous;
	}

	node->next = NULL;
	node->previous = NULL;

	return node;
}

struct tree_node *tree_node_first(struct tree_node *list)
{
	/* Grab the first node in this list from the parent if available. */
	if (list->parent) {
		return list->parent->child_head;
	}

	while (list && list->previous) {
		list = list->previous;
	}

	return list;
}

/* XXX consider talloc destructors */
void tree_node_free(struct tree_node *node)
{
	SMB_ASSERT(node->child_head == NULL);
	SMB_ASSERT(node->next == NULL);
	SMB_ASSERT(node->previous == NULL);
	talloc_free(node->name);
	talloc_free(node);
}

void tree_node_free_recursive(struct tree_node *list)
{
	struct tree_node *node;

	if (list == NULL) {
		return;
	}

	while ((node = tree_node_pop(&list)) != NULL) {
		if (node->child_head) {
			tree_node_free_recursive(node->child_head);
		}
		node->child_head = NULL;
		tree_node_free(node);
	}
}

static void tree_view_free_current_items(struct tree_view *view)
{
	size_t i;
	struct tree_node *tmp;
	ITEM *item;

	if (view->current_items == NULL) {
		return;
	}

	for (i = 0; view->current_items[i] != NULL; ++i) {
		item = view->current_items[i];
		tmp = item_userptr(item);
		if (tmp && tmp->label) {
			talloc_free(tmp->label);
			tmp->label = NULL;
		}
		free_item(item);
	}
	talloc_free(view->current_items);
	view->current_items = NULL;
}

WERROR tree_view_update(TALLOC_CTX *ctx, struct tree_view *view, struct tree_node *list)
{
	ITEM **items;
	struct tree_node *tmp;
	size_t i, n_items;

	if (list == NULL) {
		list = view->root;
	}
	for (n_items = 0, tmp = list; tmp != NULL; tmp = tmp->next) {
		n_items++;
	}
	items = talloc_zero_array(ctx, ITEM **, n_items + 1);
	W_ERROR_HAVE_NO_MEMORY(items);
	
	for (i = 0, tmp = list; tmp != NULL; ++i, tmp = tmp->next) {
		const char *label = tmp->name;

		/* Add a '+' marker to indicate that the item has
		   descendants. */
		if (tmp->child_head) {
			SMB_ASSERT(tmp->label == NULL);
			tmp->label = talloc_asprintf(ctx, "+%s", tmp->name);
			W_ERROR_HAVE_NO_MEMORY(tmp->label);
			label = tmp->label;
		}

		items[i] = new_item(label, tmp->name);
		set_item_userptr(items[i], tmp);
	}

	unpost_menu(view->menu);
	set_menu_items(view->menu, items);
	tree_view_free_current_items(view);
	view->current_items = items;

	return WERR_OK;
}

void tree_view_show(struct tree_view *view)
{
	post_menu(view->menu);
	wrefresh(view->window);
}

struct tree_view *tree_view_new(TALLOC_CTX *ctx, struct tree_node *root,
	WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x)
{
	struct tree_view *view;
	static const char *dummy = "12345";
	
	view = talloc_zero(ctx, struct tree_view);
	if (view == NULL) {
		return NULL;
	}

	view->window = orig;
	view->sub_window = derwin(orig, nlines, ncols, begin_y, begin_x);
	view->root = root;

	view->current_items = talloc_zero_array(ctx, ITEM **, 2);
	if (view->current_items == NULL) {
		return NULL;
	}
	view->current_items[0] = new_item(dummy, dummy);

	view->menu = new_menu(view->current_items);
	set_menu_format(view->menu, nlines, 1);
	set_menu_win(view->menu, view->window);
	set_menu_sub(view->menu, view->sub_window);
	menu_opts_off(view->menu, O_SHOWDESC);
	set_menu_mark(view->menu, "* ");

	tree_view_update(ctx, view, root);

	return view;
}

void tree_view_free(struct tree_view *view)
{
	unpost_menu(view->menu);
	free_menu(view->menu);
	tree_view_free_current_items(view);
	tree_node_free_recursive(view->root);
}

static void print_path_recursive(WINDOW *label, struct tree_node *node)
{
	if (node->parent)
		print_path_recursive(label, node->parent);

	wprintw(label, "%s/", node->name);
}

/* print the path of node to label */
void tree_node_print_path(WINDOW *label, struct tree_node *node)
{
	if (node == NULL)
		return;

	wmove(label, 0, 0);
	wclrtoeol(label);
	wprintw(label, "/");
	wmove(label, 0, 0);

	if (node->parent)
		print_path_recursive(label, node->parent);

	wnoutrefresh(label);
	wrefresh(label);
}
