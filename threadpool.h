#ifndef _THREADPOOL_
#define _THREADPOOL_

#define MAX_POOL	200
#define INIT_POOL	20
#define STACKSIZE	1048576

typedef void (*func_pointer) (void *);

void*	tp_init(int max, int init);

int	tp_add(void *pool, func_pointer fp, void *arg);

void*	tp_drop(void *pool);

void	tv_sub(struct timeval *out, const struct timeval *in);

#endif
