#ifndef _THREADPOOL_
#define _THREADPOOL_

#define MAX_POOL	800
#define INIT_POOL	700
#define STACKSIZE	1048576			//must larget than 16384

typedef void (*func_pointer) (void *);

void*	tp_init(int max, int init);

int	tp_add(void *pool, func_pointer fp, void *arg);

void*	tp_drop(void *pool);

void	tv_sub(struct timeval *out, const struct timeval *in);

#endif
