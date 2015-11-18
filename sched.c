#define _GNU_SOURCE
#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#define printf_once(fmt, ...)			\
({						\
	static int _printf_once = 0;		\
						\
	if (!_printf_once) {			\
		_printf_once = 1;		\
		printf(fmt, ##__VA_ARGS__);	\
	}					\
})

void err_msg(const char *format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	vprintf(format, arg_list);
	va_end(arg_list);
}

void err_exit(const char *format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	vprintf(format, arg_list);
	va_end(arg_list);

	exit(EXIT_FAILURE);
}

#define TRACE_SIZE 256
int ftrace(int fd, size_t size, const char *format, ...)
{
	char entry[size];
	va_list arg_list;

	va_start(arg_list, format);
	vsnprintf(entry, size, format, arg_list);
	va_end(arg_list);

	if (write(fd, entry, strlen(entry)) != strlen(entry))
		return 1;

	return 0;
}

struct list_head {
	struct list_head *prev;
	struct list_head *next;

	int len;
};

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_next_entry(pos, member))

void list_init(struct list_head *list)
{
	list->prev = list;
	list->next = list;

	list->len = 0;
}

void list_add(struct list_head *head, struct list_head *new)
{
	struct list_head *prev = head->prev;

	head->prev = new;
	new->next = head;
	new->prev = prev;
	prev->next = new;

	head->len++;
}

void list_del(struct list_head *head, struct list_head *entry)
{
	head->next = entry->next;
	entry->next = entry;
	entry->prev = entry;

	head->len--;
}

int list_empty(struct list_head *head)
{
	return head->next == head;
}

struct rb_node
{
	intptr_t rb_parent_color;
#define	RB_RED		0
#define	RB_BLACK	1
	struct rb_node *rb_right;
	struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long))));

struct rb_root
{
	struct rb_node *rb_node;
};

struct rb_tree {
	struct rb_root root;
	struct rb_node *leftmost;

	unsigned int dim;
};

#define rb_parent(r)   ((struct rb_node *)((r)->rb_parent_color & ~3))
#define rb_color(r)   ((r)->rb_parent_color & 1)
#define rb_is_red(r)   (!rb_color(r))
#define rb_is_black(r) rb_color(r)
#define rb_set_red(r)  do { (r)->rb_parent_color &= ~1; } while (0)
#define rb_set_black(r)  do { (r)->rb_parent_color |= 1; } while (0)

#define RB_ROOT	(struct rb_root) { NULL, }
#define	rb_entry(ptr, type, member) container_of(ptr, type, member)

#define RB_EMPTY_ROOT(root)	((root)->rb_node == NULL)
#define RB_EMPTY_NODE(node)	(rb_parent(node) == node)
#define RB_CLEAR_NODE(node)	(rb_set_parent(node, node))

void rb_tree_init(struct rb_tree *t)
{
	t->root = RB_ROOT;
	t->leftmost = NULL;
	t->dim = 0;
}

void rb_set_parent(struct rb_node *rb, struct rb_node *p)
{
	rb->rb_parent_color = (rb->rb_parent_color & 3) | (uintptr_t)p;
}

void rb_set_color(struct rb_node *rb, int color)
{
	rb->rb_parent_color = (rb->rb_parent_color & ~1) | color;
}

void rb_link_node(struct rb_node * node, struct rb_node * parent,
		  struct rb_node ** rb_link)
{
	node->rb_parent_color = (uintptr_t)parent;
	node->rb_left = node->rb_right = NULL;

	*rb_link = node;
}

void __rb_rotate_left(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *right = node->rb_right;
	struct rb_node *parent = rb_parent(node);

	if ((node->rb_right = right->rb_left))
		rb_set_parent(right->rb_left, node);
	right->rb_left = node;

	rb_set_parent(right, parent);

	if (parent)
	{
		if (node == parent->rb_left)
			parent->rb_left = right;
		else
			parent->rb_right = right;
	}
	else
		root->rb_node = right;
	rb_set_parent(node, right);
}

void __rb_rotate_right(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *left = node->rb_left;
	struct rb_node *parent = rb_parent(node);

	if ((node->rb_left = left->rb_right))
		rb_set_parent(left->rb_right, node);
	left->rb_right = node;

	rb_set_parent(left, parent);

	if (parent)
	{
		if (node == parent->rb_right)
			parent->rb_right = left;
		else
			parent->rb_left = left;
	}
	else
		root->rb_node = left;
	rb_set_parent(node, left);
}

void rb_insert_color(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *parent, *gparent;

	while ((parent = rb_parent(node)) && rb_is_red(parent)) {
		gparent = rb_parent(parent);

		if (parent == gparent->rb_left) {
			{
				register struct rb_node *uncle = 
							gparent->rb_right;
				if (uncle && rb_is_red(uncle)) {
					rb_set_black(uncle);
					rb_set_black(parent);
					rb_set_red(gparent);
					node = gparent;
					continue;
				}
			}

			if (parent->rb_right == node) {
				register struct rb_node *tmp;
				__rb_rotate_left(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			rb_set_black(parent);
			rb_set_red(gparent);
			__rb_rotate_right(gparent, root);
		} else {
			{
				register struct rb_node *uncle = 
							gparent->rb_left;
				if (uncle && rb_is_red(uncle)) {
					rb_set_black(uncle);
					rb_set_black(parent);
					rb_set_red(gparent);
					node = gparent;
					continue;
				}
			}

			if (parent->rb_left == node) {
				register struct rb_node *tmp;
				__rb_rotate_right(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			rb_set_black(parent);
			rb_set_red(gparent);
			__rb_rotate_left(gparent, root);
		}
	}

	rb_set_black(root->rb_node);
}

void __rb_erase_color(struct rb_node *node, struct rb_node *parent,
			     struct rb_root *root)
{
	struct rb_node *other;

	while ((!node || rb_is_black(node)) && node != root->rb_node) {
		if (parent->rb_left == node) {
			other = parent->rb_right;
			if (rb_is_red(other)) {
				rb_set_black(other);
				rb_set_red(parent);
				__rb_rotate_left(parent, root);
				other = parent->rb_right;
			}
			if ((!other->rb_left || 
				rb_is_black(other->rb_left)) &&
			    (!other->rb_right || 
				rb_is_black(other->rb_right))) {
				rb_set_red(other);
				node = parent;
				parent = rb_parent(node);
			} else {
				if (!other->rb_right || 
					rb_is_black(other->rb_right)) {
					struct rb_node *o_left;

					if ((o_left = other->rb_left))
						rb_set_black(o_left);
					rb_set_red(other);
					__rb_rotate_right(other, root);
					other = parent->rb_right;
				}
				rb_set_color(other, rb_color(parent));
				rb_set_black(parent);
				if (other->rb_right)
					rb_set_black(other->rb_right);
				__rb_rotate_left(parent, root);
				node = root->rb_node;
				break;
			}
		} else {
			other = parent->rb_left;
			if (rb_is_red(other)) {
				rb_set_black(other);
				rb_set_red(parent);
				__rb_rotate_right(parent, root);
				other = parent->rb_left;
			}
			if ((!other->rb_left || 
				rb_is_black(other->rb_left)) &&
			    (!other->rb_right || 
				rb_is_black(other->rb_right))) {
				rb_set_red(other);
				node = parent;
				parent = rb_parent(node);
			} else {
				if (!other->rb_left || 
					rb_is_black(other->rb_left)) {
					register struct rb_node *o_right;

					if ((o_right = other->rb_right))
						rb_set_black(o_right);
					rb_set_red(other);
					__rb_rotate_left(other, root);
					other = parent->rb_left;
				}
				rb_set_color(other, rb_color(parent));
				rb_set_black(parent);
				if (other->rb_left)
					rb_set_black(other->rb_left);
				__rb_rotate_right(parent, root);
				node = root->rb_node;
				break;
			}
		}
	}
	if (node)
		rb_set_black(node);
}

void rb_erase(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *child, *parent;
	int color;

	if (!node->rb_left) {
		child = node->rb_right;
	} else if (!node->rb_right) {
		child = node->rb_left;
	} else {
		struct rb_node *old = node, *left;

		node = node->rb_right;
		while ((left = node->rb_left) != NULL)
			node = left;
		child = node->rb_right;
		parent = rb_parent(node);
		color = rb_color(node);

		if (child) {
			rb_set_parent(child, parent);
		} if (parent == old) {
			parent->rb_right = child;
			parent = node;
		} else {
			parent->rb_left = child;
		}

		node->rb_parent_color = old->rb_parent_color;
		node->rb_right = old->rb_right;
		node->rb_left = old->rb_left;

		if (rb_parent(old)) {
			if (rb_parent(old)->rb_left == old)
				rb_parent(old)->rb_left = node;
			else
				rb_parent(old)->rb_right = node;
		} else {
			root->rb_node = node;
		}

		rb_set_parent(old->rb_left, node);
		if (old->rb_right)
			rb_set_parent(old->rb_right, node);
		goto color;
	}

	parent = rb_parent(node);
	color = rb_color(node);

	if (child)
		rb_set_parent(child, parent);
	if (parent) {
		if (parent->rb_left == node)
			parent->rb_left = child;
		else
			parent->rb_right = child;
	} else {
		root->rb_node = child;
	}
color:
	if (color == RB_BLACK)
		__rb_erase_color(child, parent, root);
}

struct rb_node *rb_first(struct rb_root *root)
{
	struct rb_node	*n;

	n = root->rb_node;
	if (!n)
		return NULL;
	while (n->rb_left)
		n = n->rb_left;
	return n;
}

struct rb_node *rb_next(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;

	if (node->rb_right) {
		node = node->rb_right; 
		while (node->rb_left)
			node=node->rb_left;
		return (struct rb_node *)node;
	}

	while ((parent = rb_parent(node)) && node == parent->rb_right)
		node = parent;

	return parent;
}

void swap(void *a, void *b, int size)
{
	char t;

	do {
		t = *(char *)a;
		*(char *)a++ = *(char *)b;
		*(char *)b++ = t;
	} while (--size > 0);
}

void sort(void *base, size_t num, size_t size,
	  int (*cmp)(const void *, const void *))
{
	int i = (num/2 - 1) * size, n = num * size, c, r;

	for ( ; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r  = c) {
			c = r * 2 + size;
			if (c < n - size 
			    && cmp(base + c, base + c + size) < 0)
				c += size;
			if (cmp(base + r, base + c) >= 0)
				break;
			swap(base + r, base + c, size);
		}
	}

	for (i = n - size; i > 0; i -= size) {
		swap(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size
			    && cmp(base + c, base + c + size) < 0)
				c += size;
			if (cmp(base + r, base + c) >= 0)
				break;
			swap(base + r, base + c, size);
		}
	}
}

#define PRIO_MIN	0
#define PRIO_MAX	100
struct vert {
	int id;			/* vertex id				*/
	char name[256];		/* vertex name				*/

	struct list_head pred;	/* list of predecessors			*/
	struct list_head succ;	/* list of successors			*/

	struct task *t;		/* task this vertex belongs to		*/
	struct cl_node *n;	/* node this vertex belongs to		*/

	double e;		/* execution time			*/
	double prob;		/* probability WCET < e			*/
	double l_to;		/* len of crit.path to this vertex	*/
	double l_from;		/* len of crit.path from this vertex	*/
	double u;		/* utilization				*/

	double resp;		/* response time			*/
	double tard;		/* resp - d				*/

	double x;		/* X_v (for RTA)			*/
	double y;		/* Y_v (for RTA)			*/

	int prio;		/* priority of this vertex		*/

	struct rb_node node;
};
struct rb_tree verts;

int vert_before(struct vert *v1, struct vert *v2)
{
	return strcmp(v1->name, v2->name) < 0;
}

int verts_insert(struct rb_tree *verts, struct vert *v)
{
	struct rb_node **link = &verts->root.rb_node;
	struct rb_node *parent = NULL;
	struct vert *entry;
	int leftmost = 1;

	if (!RB_EMPTY_NODE(&v->node))
		return 1;

	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct vert, node);
		if (vert_before(v, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	if (leftmost)
		verts->leftmost = &v->node;

	rb_link_node(&v->node, parent, link);
	rb_insert_color(&v->node, &verts->root);

	verts->dim++;
	return 0;
}

struct vert *verts_search(struct rb_tree *verts,
			  const char *name)
{
        struct rb_node *node = verts->root.rb_node;
	struct vert val;

	memset(val.name, 0, 256);
	strcpy(val.name, name);

        while (node) {
		struct vert *v = rb_entry(node, struct vert, node);

		if (vert_before(&val, v))
			node = node->rb_left;
		else if (vert_before(v, &val))
			node = node->rb_right;
		else
			return v;
  	}

	return NULL;
}

#define CPUNODE	0
#define IONODE	1
struct cl_node {
	char name[256];		/* node name				*/
	int type;		/* node type (CPUNODE or IONODE)	*/

	int cpus;		/* # of CPUs in this node (if CPUNODE)  */

	struct list_head lnode;
};
struct list_head nodes;

struct task {
	int id;			/* task id				*/
	char name[256];		/* task name				*/

	struct vert *v;		/* vertices				*/
	int nv;			/* number of vertices			*/

	double d;		/* deadline				*/
	double p;		/* period				*/

	double vol;		/* volume				*/
	double len;		/* length				*/
	double u;		/* utilization				*/

	double resp;		/* response time			*/
	double tard;		/* resp - d				*/
};

struct taskset {
	struct task *t;		/* tasks				*/
	int nt;			/* number of tasks			*/

	double u;		/* utilization				*/
};

struct _vert {
	int id;
	struct list_head lnode;
};

int vert_print(struct vert *v)
{
	struct _vert *_succ;
	int first = 1;

	if (!v)
		return 1;

	printf("\tchunk %s[%d]\n", v->name, v->id);
	printf("\t      priority  = %d\n", v->prio);
	printf("\t      schedNode = %s\n", v->n->name);
	printf("\t      exec.time = %.0f\n", v->e);
	printf("\t      successors: ");

	if (!list_empty(&v->succ)) {
		list_for_each_entry(_succ, &v->succ, lnode) {

			if (first)
				printf("[%d", _succ->id);
			else
				printf(", %d", _succ->id);

			first = 0;
		}

		printf("]\n");

	} else {
		printf("\n");
	}

	printf("\n");
	return 0;
}

int vert_stat(struct vert *v)
{
	struct _vert *_succ;
	int first = 1;

	if (!v)
		return 1;

	printf("%21d%14.0f     %9.0f    %s\n",
		v->id, v->resp, v->tard, (v->tard > 0) ? "     X" : "");

	return 0;
}

int task_init(struct task *t, int nv, double d, double p)
{
	int i;

	if (!t || nv <= 0)
		return 1;

	if (d < 0 || p < 0)
		return 1;

	t->v = (struct vert *)malloc(nv * sizeof(struct vert));
	if (!t->v)
		return 1;

	for (i = 0; i < nv; i++) {
		t->v[i].id = i;
		memset(t->v[i].name, 0, 256);

		list_init(&t->v[i].pred);
		list_init(&t->v[i].succ);

		t->v[i].t = t;
		t->v[i].e = 0.0;

		t->v[i].prio = PRIO_MIN;
		t->v[i].n = 0;
	}

	t->nv = nv;
	t->d = d;
	t->p = p;

	return 0;
}

int task_volume(struct task *t)
{
	int i;

	if (!t)
		return 1;

	t->vol = 0.0;
	for (i = 0; i < t->nv; i++)
		t->vol += t->v[i].e;

	if (t->p)
		t->u = t->vol / t->p;

	return 0;
}

int task_length(struct task *t)
{
	double *l_tmp;
	int i, iter;

	if (!t)
		return 1;

	l_tmp = (double *)malloc(t->nv * sizeof(double));
	if (!l_tmp)
		return;

	for (i = 0; i < t->nv; i++) {
		t->v[i].l_to = t->v[i].e;
		l_tmp[i] = t->v[i].e;
	}

	for (iter = 0; iter < t->nv; iter++) {

	for (i = 0; i < t->nv; i++) {
		struct _vert *_v;

		list_for_each_entry(_v, &t->v[i].pred, lnode) {
			if (t->v[i].e + l_tmp[_v->id] > t->v[i].l_to)
				t->v[i].l_to = t->v[i].e + l_tmp[_v->id];
		}
	}

	for (i = 0; i < t->nv; i++)
		l_tmp[i] = t->v[i].l_to;
	}

	for (i = 0; i < t->nv; i++) {
		t->v[i].l_from = t->v[i].e;
		l_tmp[i] = t->v[i].e;
	}

	for (iter = 0; iter < t->nv; iter++) {

	for (i = 0; i < t->nv; i++) {
		struct _vert *_v;

		list_for_each_entry(_v, &t->v[i].succ, lnode) {
			if (t->v[i].e + l_tmp[_v->id] > t->v[i].l_from)
				t->v[i].l_from = t->v[i].e + l_tmp[_v->id];
		}
	}

	for (i = 0; i < t->nv; i++)
		l_tmp[i] = t->v[i].l_from;
	}

	t->len = 0.0;
	for (i = 0; i < t->nv; i++) {
		if (t->v[i].l_from > t->len)
			t->len = t->v[i].l_from;
	}

	free(l_tmp);
	return 0;
}

int task_add_edge(struct task *t, int from, int to)
{
	struct vert *src, *dst; 
	struct _vert *_src, *_dst;

	if (!t || !t->v)
		return 1;

	if (from < 0 || from >= t->nv)
		return 1;

	if (to < 0 || to >= t->nv)
		return 1;

	_src = (struct _vert *)malloc(sizeof(struct _vert));
	if (!_src)
		return 1;

	_dst = (struct _vert *)malloc(sizeof(struct _vert));
	if (!_dst) {
		free(_src);
		return 1;
	}

	_src->id = from;
	_dst->id = to;
	src = &t->v[from];
	dst = &t->v[to];

	list_add(&dst->pred, &_src->lnode);
	list_add(&src->succ, &_dst->lnode);

	task_length(t);

	return 0;
}

int task_set_wcet(struct task *t, int i, double e)
{
	if (!t || !t->v)
		return 1;

	if (i < 0 || i >= t->nv)
		return 1;

	if (e <= 0)
		return 1;

	t->v[i].e = e;

	task_volume(t);
	task_length(t);
	return 0;
}

int task_reachable(struct task *t, struct vert *s, struct vert *d)
{
	struct list_head l;
	struct _vert _va[t->nv];
	int visit[t->nv], i;

	if (s->id == d->id)
		return 1;

	list_init(&l);

	for (i = 0; i < t->nv; i++) {
		_va[i].id = i;
		visit[i] = 0;
	}

	visit[s->id] = 1;
	list_add(&l, &_va[s->id].lnode);

	while (!list_empty(&l)) {
		struct _vert *_v, *_it;

		_v = list_first_entry(&l, struct _vert, lnode);
		list_del(&l, &_v->lnode);

		list_for_each_entry(_it, &t->v[_v->id].succ, lnode) {
			if (_it->id == d->id)
				return 1;

			if (!visit[_it->id]) {
				visit[_it->id] = 1;
				list_add(&l, &_va[_it->id].lnode);
			}
		}
	}
	
	return 0;
}

int task_print(struct task *t)
{
	char id[256];
	int i;

	if (!t || !t->v)
		return 1;

	snprintf(id, 256, "TASK \"%s\"[%d]:", t->name, t->id);

	printf("%s   # of vertices = %d,  D = %.0f,  T = %.0f\n\n",
		id, t->nv, t->d, t->p);

	for (i = 0; i < t->nv; i++)
		vert_print(&t->v[i]);

	printf("  len = %.0f,  vol = %.0f,  util. = %.2f\n\n",
		t->len, t->vol, t->u);

	return 0;
}

int task_stat(struct task *t)
{
	char id[10];
	int i;

	if (!t || !t->v)
		return 1;

	snprintf(id, 10, "TASK %d", t->id);

	printf("\n%10s:  resp.time = %.0f,  tardiness = %.0f (D = %.0f)\n\n",
		id, t->resp, t->tard, t->d);

	printf("               vertex     resp.time     tardiness     "
		"dead.miss\n");
	for (i = 0; i < t->nv; i++)
		vert_stat(&t->v[i]);

	printf("\n");
	return 0;
}

int task_finalize(struct task *t)
{
	int i;

	if (!t || !t->v)
		return 1;

	for (i = 0; i < t->nv; i++) {
		struct _vert *_v;

		list_for_each_entry(_v, &t->v[i].pred, lnode) 
			free(_v);

		list_for_each_entry(_v, &t->v[i].succ, lnode) 
			free(_v);
	}
	
	free(t->v);
	return 0;
}

int taskset_init(struct taskset *ts, int nt)
{
	int i;

	if (!ts || nt <= 0)
		return 1;

	srand(time(NULL));

	ts->t = (struct task *)malloc(nt * sizeof(struct task));
	if (!ts->t)
		return 1;

	for (i = 0; i < nt; i++) {
		ts->t[i].id = i;
		memset(ts->t[i].name, 0, 256);

		ts->t[i].d = 0.0;
		ts->t[i].p = 0.0;
	}

	ts->nt = nt;
	return 0;
}

int taskset_print(struct taskset *ts)
{
	int i;

	if (!ts || !ts->t)
		return 1;

	printf("********************************************************"
		"*******************\n");
	printf("   TASKSET:   # of tasks = %d,   tot. utilization = %.2f\n", 
		ts->nt, ts->u);
	printf("********************************************************"
		"*******************\n");

	for (i = 0; i < ts->nt; i++) {
		task_print(&ts->t[i]);
		printf("---------------------------------------------------"
			"------------------------\n");
	}

	return 0;
}

int taskset_stat(struct taskset *ts)
{
	double prob = 1.0;
	int i, j;

	if (!ts || !ts->t)
		return 1;

	for (i = 0; i < ts->nt; i++) {
		struct task *t = &ts->t[i];

		for (j = 0; j < t->nv; j++) {
			struct vert *v = &t->v[j];

			prob *= v->prob;
		}
	}

	printf("********************************************************"
		"*******************\n");
	printf("   TASKSET STATISTICS  (confidence >= %.2f)\n", prob); 
	printf("********************************************************"
		"*******************\n");

	for (i = 0; i < ts->nt; i++) {
		task_stat(&ts->t[i]);
		printf("---------------------------------------------------"
			"------------------------\n");
	}

	return 0;
}

xmlNode *xml_find_child(xmlNode *root, const char *name)
{
	xmlNode *node = root->children;

	while (node) {
		if (strcmp(node->name, name) == 0)
			return node;

		node = node->next;
	}

	return NULL;
}

int xml_parse_stream_specification(struct task *t, xmlNode *root)
{
	xmlNode *node;
	xmlAttr *attr;

	node = xml_find_child(root, "relDl");
	if (!node)
		return 1;

	t->d = atoi(xmlNodeGetContent(node));

	node = xml_find_child(root, "occKind");
	if (!node)
		return 1;

	attr = node->properties;
	while (attr) {
		if (strcmp(attr->name, "period") == 0) {
			t->p = atoi(xmlNodeGetContent(attr->children));
			break;
		}

		attr = attr->next;
	}

	return 0;
}

int xml_parse_stream(struct task *t, xmlNode *root)
{
	xmlNode *node = xml_find_child(root, "rtSpecification");

	if (!node)
		return 1;

	if (xml_parse_stream_specification(t, node))
		return 1;

	return 0;
}

int xml_parse_program(struct task *t, xmlNode *root)
{
	xmlNode *node = xml_find_child(root, "requestResponseStream");

	if (!node)
		return 0;

	if (xml_parse_stream(t, node))
		return 1;

	return 0;
}

int xml_parse_software(struct task *t, xmlNode *root)
{
	xmlNode *node = root->children;

	while (node) {

		if (strcmp(node->name, "program") == 0) {
			if (xml_parse_program(t, node))
				return 1;

			if (t->d && t->p)
				break;
		}

		node = node->next;
	}

	return 0;
}

int xml_count_verts(struct task *t, xmlNode *root)
{
	xmlNode *node = root->children;

	t->nv = 0;

	while (node) {
		if (strcmp(node->name, "chunk") == 0)
			t->nv++;

		node = node->next;
	}

	return 0;
}

int xml_parse_vert_specification(struct task *t, int i, xmlNode *root)
{
	xmlNode *node = root->children;

	while (node) {
		xmlAttr *attr = node->properties;

		while (attr) {
			if (strcmp(attr->name, "worst") == 0) {
				if (task_set_wcet(t, i, atoi(
				     xmlNodeGetContent(attr->children))))
					return 1;
			}

			if (strcmp(attr->name, "prob") == 0)
				t->v[i].prob = atof(
				     xmlNodeGetContent(attr->children));

			attr = attr->next;
		}

		node = node->next;
	}

	return 0;
}

int xml_parse_vert(struct task *t, int i, xmlNode *root)
{
	xmlAttr *attr = root->properties;
	xmlNode *node;

	while (attr) {

		if (strcmp(attr->name, "id") == 0)
			strcpy(t->v[i].name,
			       xmlNodeGetContent(attr->children));

		if (strcmp(attr->name, "schedNode") == 0) {
			struct cl_node *n;

			list_for_each_entry(n, &nodes, lnode) {
				if (strcmp(n->name, xmlNodeGetContent(
						attr->children)) == 0) {
					t->v[i].n = n;
					break;
				}
			}
		}

		attr = attr->next;
	}

	node = xml_find_child(root, "rtSpecification");
	if (!node)
		return 1;

	attr = node->properties;
	while (attr) {
		if (strcmp(attr->name, "priority") == 0)
			t->v[i].prio = atoi(
					xmlNodeGetContent(attr->children));

		attr = attr->next;
	}

	if (xml_parse_vert_specification(t, i, node))
		return 1;

	return 0;
}

int xml_parse_verts(struct task *t, xmlNode *root)
{
	xmlNode *node = root->children;
	int i = 0;

	rb_tree_init(&verts);

	while (node) {

		if (strcmp(node->name, "chunk") == 0) {
			if (xml_parse_vert(t, i, node))
				return 1;

			RB_CLEAR_NODE(&t->v[i].node);
			if (verts_insert(&verts, &t->v[i]))
				return 1;

			i++;
		}

		node = node->next;
	}

	return 0;
}

int __xml_parse_succ(struct task *t, int i, xmlNode *root)
{
	xmlAttr *attr = root->properties;

	while (attr) {
		if (strcmp(attr->name, "id") == 0) {
			struct vert *v = verts_search(&verts,
					xmlNodeGetContent(attr->children));

			if (!v)
				return 1;

			if (task_add_edge(t, i, v->id))
				return 1;
		}

		attr = attr->next;
	}

	return 0;
}

int xml_parse_succ(struct task *t, int i, xmlNode *root)
{
	xmlNode *node = root->children;

	while (node) {
		if (strcmp(node->name, "successor") == 0) {
			if (__xml_parse_succ(t, i, node))
				return 1;
		}

		node = node->next;
	}

	return 0;
}

int xml_parse_succs(struct task *t, xmlNode *root)
{
	xmlNode *node = root->children;
	int i = 0;

	while (node) {

		if (strcmp(node->name, "chunk") == 0) {
			if (xml_parse_succ(t, i, node))
				return 1;

			i++;
		}

		node = node->next;
	}

	return 0;
}

int xml_parse_behavior(struct task *t, xmlNode *root)
{
	xmlNode *node = root->children;

	while (node) {

		if (strcmp(node->name, "behaviorSpecification") == 0) {
			xml_count_verts(t, node);

			if (task_init(t, t->nv, t->d, t->p))
				return 1;

			if (xml_parse_verts(t, node))
				return 1;

			if (xml_parse_succs(t, node))
				return 1;
		}

		node = node->next;
	}

	return 0;
}

int xml_node_type(struct cl_node *n, xmlNode *root)
{
	xmlNode *node = root->children;
	
	n->cpus = 0;

	while (node) {
		if (strcmp(node->name, "cpu") == 0) {
			n->type = CPUNODE;
			n->cpus++;
		}

		if (strcmp(node->name, "disk") == 0 ||
	            strcmp(node->name, "net") == 0) {
			n->type = IONODE;
			break;
		}

		node = node->next;
	}

	return 0;
}

int xml_parse_node(xmlNode *root)
{
	xmlAttr *attr = root->properties;
	struct cl_node *n;

	n = (struct cl_node *)malloc(sizeof(struct cl_node));
	if (!n)
		return 1;
	memset(n->name, 0, 256);

	while (attr) {
		if (strcmp(attr->name, "name") == 0) {
			strcpy(n->name, xmlNodeGetContent(attr->children));
			break;
		}

		attr = attr->next;
	}

	xml_node_type(n, root);
	list_add(&nodes, &n->lnode);
	return 0;
}

int xml_parse_hardware(struct task *t, xmlNode *root)
{
	xmlNode *node = root->children;

	list_init(&nodes);

	while (node) {

		if (strcmp(node->name, "schedNode") == 0) {
			if (xml_parse_node(node))
				return 1;
		}

		node = node->next;
	}

	return 0;
}

int xml_task_parse(struct task *t, xmlNode *root)
{
	xmlNode *node;

	node = xml_find_child(root, "softwareModel");
	if (!node)
		return 1;
	if (xml_parse_software(t, node))
		return 1;

	node = xml_find_child(root, "schedModel");
	if (!node)
		return 1;
	if (xml_parse_hardware(t, node))
		return 1;

	node = xml_find_child(root, "behaviorModel");
	if (!node)
		return 1;
	if (xml_parse_behavior(t, node))
		return 1;

	return 0;
}

int xml_count_tasks(struct taskset *ts, xmlNode *root)
{
	xmlNode *node = root->children;

	ts->nt = 0;

	while (node) {
		if (strcmp(node->name, "application") == 0)
			ts->nt++;

		node = node->next;
	}

	return 0;
}

int xml_task_name(struct task *t, xmlNode *root)
{
	xmlAttr *attr = root->properties;

	while (attr) {
		if (strcmp(attr->name, "name") == 0) {
			strcpy(t->name, xmlNodeGetContent(attr->children));
			break;
		}

		attr = attr->next;
	}

	return 0;
}

int xml_parse_tasks(struct taskset *ts, xmlNode *root)
{
	xmlNode *node = root->children;
	int i = 0;

	while (node) {
		if (strcmp(node->name, "application") == 0) {
			xml_task_name(&ts->t[i], node);

			if (xml_task_parse(&ts->t[i], node))
				return 1;

			i++;
		}

		node = node->next;
	}

	ts->u = 0.0;
	for (i = 0; i < ts->nt; i++)
		ts->u += ts->t[i].u;

	return 0;
}

int xml_validate(struct taskset *ts)
{
	int i, j;

	if (ts->nt < 1)
		return 1;

	for (i = 0; i < ts->nt; i++) {
		struct task *t = &ts->t[i];

		if (t->nv < 1)
			return 1;
		if (t->d < 1.0 || t->p < 1.0)
			return 1;
		if (strcmp(t->name, "") == 0)
			return 1;

		for (j = 0; j < t->nv; j++) {
			struct vert *v = &t->v[j];

			if (!v->t || !v->n)
				return 1;
			if (v->n->type != CPUNODE && v->n->type != IONODE)
				return 1;
			if (v->n->type == CPUNODE && v->n->cpus < 1)
				return 1;

			if (v->e < 1.0)
				return 1;
			if (v->prob < 0.0 || v->prob > 1.0)
				return 1;
			if (v->prio < PRIO_MIN || v->prio > PRIO_MAX)
				return 1;
			if (strcmp(v->name, "") == 0)
				return 1;
		}
	}

	return 0;
}

int taskset_parse(struct taskset *ts, const char *file)
{
	xmlDoc *doc = NULL;
	xmlNode *root = NULL;
	int i;

	LIBXML_TEST_VERSION

	doc = xmlReadFile(file, NULL, 0);
	if (doc == NULL)
		return 1;

	root = xmlDocGetRootElement(doc);

	xml_count_tasks(ts, root);
	if (taskset_init(ts, ts->nt))
		return 1;

	if (xml_parse_tasks(ts, root))
		return 1;

	if (xml_validate(ts))
		return 1;

	xmlFreeDoc(doc);
	xmlCleanupParser();

	return 0;
}

int taskset_finalize(struct taskset *ts)
{
	int i;

	if (!ts || !ts->t)
		return 1;

	for (i = 0; i < ts->nt; i++)
		task_finalize(&ts->t[i]);

	free(ts->t);
	return 0;
}

#define max(a, b)	((a > b) ? a : b)
#define min(a, b)	((a < b) ? a : b)

#define XI	16
#define TARD_TOL	10000000000.0
int rta(struct taskset *ts, int *sched)
{
	int x_schedule, y_update;
	int i, j, k, l, nu;

	if (!ts)
		return 1;

	for (i = 0; i < ts->nt; i++) {
		struct task *t = &ts->t[i];

		for (j = 0; j < t->nv; j++) {
			struct vert *v = &t->v[j];

			v->x = v->e;
			v->y = t->d + 1.0;
		}
	}

	nu = 1;

next_fixedpoint:
	x_schedule = 1;
	y_update = 0;

	for (i = 0; i < ts->nt; i++) {
		struct task *t = &ts->t[i];

		for (j = 0; j < t->nv; j++) {
			struct vert *v = &t->v[j];
			double next_x, pred;
			struct _vert *_v;

next_iteration:
			next_x = 0.0;

			if (v->n->type == IONODE)
				goto IO_operation;

			for (k = 0; k < ts->nt; k++) {
				struct task *ti = &ts->t[k];

				for (l = 0; l < ti->nv; l++) {
					struct vert *vi = &ti->v[l];
					double wi;

					if (vi->prio < v->prio)
						continue;

					if (vi->n != v->n)
						continue;

					wi = max(ceil((vi->y + v->x) / ti->p),
						 0.0);

					next_x += wi * vi->e;

					if (ti->id == t->id) {
						if (vi->id != v->id &&
						    task_reachable(t, v, vi))
							next_x -= vi->e;
					}
				}
			}

			next_x = floor(next_x / (double)v->n->cpus);
IO_operation:
			next_x += v->e;

			pred = 0.0;
			list_for_each_entry(_v, &v->pred, lnode) {
				if (pred < t->v[_v->id].y)
					pred = t->v[_v->id].y;
			}
			next_x += pred;

			if (next_x > t->d + TARD_TOL - 1.0)
				next_x = t->d + TARD_TOL;

			if (next_x > v->x) {
				v->x = next_x;
				goto next_iteration;
			}

			if (v->x > t->d)
				x_schedule &= 0;

			if (v->x < v->y)
				y_update |= 1;
		}
	}

	nu = nu + 1;
	if ((nu > XI && XI != 0) || !y_update) {
		if (x_schedule)
			goto fixedpoint_schedule;

		*sched = 0;

		for (i = 0; i < ts->nt; i++) {
			struct task *t = &ts->t[i];

			t->resp = 0.0;
			for (j = 0; j < t->nv; j++) {
				struct vert *v = &t->v[j];

				v->resp = v->x;
				v->tard = v->resp - t->d;

				if (v->resp > t->resp) {
					t->resp = v->resp;
					t->tard = t->resp - t->d;
				}
			}
		}

		return 0;
	}

	for (i = 0; i < ts->nt; i++) {
		struct task *t = &ts->t[i];

		for (j = 0; j < t->nv; j++) {
			struct vert *v = &t->v[j];
			double next_x;

			v->y = v->x;
			v->x = v->e;
		}
	}

	goto next_fixedpoint;

fixedpoint_schedule:
	*sched = 1;

	for (i = 0; i < ts->nt; i++) {
		struct task *t = &ts->t[i];

		t->resp = 0.0;
		for (j = 0; j < t->nv; j++) {
			struct vert *v = &t->v[j];

			v->resp = v->x;
			v->tard = v->resp - t->d;

			if (v->resp > t->resp) {
				t->resp = v->resp;
				t->tard = t->resp - t->d;
			}
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct taskset ts;
	int sched;

	if (argc != 2)
		err_exit("Usage: dag <file.xml>\n");

	if (taskset_parse(&ts, argv[1]))
		err_exit("ERROR parsing XML file %s\n", argv[1]);

	taskset_print(&ts);

	if (rta(&ts, &sched))
		err_exit("ERROR running sched. test\n"); 

	if (sched) {
		printf("\nThe taskset is schedulable"
			" according to RTA:\n");
		taskset_stat(&ts);
	} else {
		printf("\nThe taskset is NOT schedulable"
			" according to RTA.\n");
		taskset_stat(&ts);
	}

	if (taskset_finalize(&ts))
		err_exit("taskset_finalize\n");

	return 0;
}
