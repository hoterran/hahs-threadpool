#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "threadpool.h"
#include "log.h"
#include "utils.h"

typedef struct _Thread
{
	pthread_t	id;
	pthread_mutex_t mutex;
	pthread_cond_t	cond;
	func_pointer	func;
	void 		*arg;
	void	        *parent;
} Thread_t;

typedef struct _Threadpool_t
{
	pthread_mutex_t mutex;
	pthread_cond_t  idle_cond;		/*拿到条件 意味着有空线程 可利用		*/
	pthread_cond_t	allidle_cond;		/*拿到条件 意味着线程都闲置了　 		*/
	pthread_cond_t  drop_cond;		/*拿到条件 意味着线程都退出了，可删除线程池了	*/
	Thread_t	**thread_list;		/*线程数组				*/
	int		thread_idle_num;	/*空闲线程个数				*/
	int 		thread_hwm_num;		/*创建线程的高水位				*/
	int		thread_max_num;
	int		is_close;		/*线程池关闭标志 1 是要关闭			*/
} Tp_t;

void*	t_thread(void *arg);
int	t_idle(Tp_t *pool, Thread_t *thread);
void	tv_sub(struct timeval *out,const struct timeval *in);
void	init_func(void* arg);

void* tp_init (int max, int init)
{
	int i;
	ASSERT(max > 0);
	ASSERT(init >= 0);
	ASSERT(max >= init);

	Tp_t *pool = NULL;
		
	if (NULL == (pool = (Tp_t*)malloc(sizeof(Tp_t)))) {
		return NULL;
	}
	
	if (NULL == (pool->thread_list = 
		(Thread_t**)malloc(sizeof(Thread_t*) * max))) 
	{
		free(pool);
		pool = NULL;
		return NULL;
	}

	memset(pool->thread_list, 0, sizeof(Thread_t*) * max);

	pthread_mutex_init(&pool->mutex,NULL);
	pthread_cond_init(&pool->idle_cond,NULL);
	pthread_cond_init(&pool->allidle_cond,NULL);	
	pthread_cond_init(&pool->drop_cond,NULL);
	
	pool->thread_idle_num = 0;
	pool->thread_hwm_num = 0;
	pool->is_close	= 0;
	pool->thread_max_num = max;

	dump(L_SUCCESS, "create thread pool %d successfully!!", pool->thread_max_num);

	for (i = 0; i < init; i++) {
		if (-1 == tp_add(pool, init_func, NULL)) {
			free(pool->thread_list);
			pool->thread_list = NULL;
			free(pool);
			pool = NULL;
			return NULL;
		}
	}

	dump(L_SUCCESS, "init thread pool %d/%d successfully!!", 
		pool->thread_hwm_num, pool->thread_max_num);

	return (void *)pool;
}

/* execute job and then idle thread, then cond_wait , if is_close ,then return*/
void* t_thread(void *arg)
{
	if (NULL == arg) return NULL;

	Thread_t *thread =(Thread_t*)arg;
	Tp_t *pool = thread->parent;

	/*
		change while to do while for
		a thread add, then quickly pool drop 
		the thread possible cant execute once
	*/
	do {
		struct timeval start_time;
		struct timeval end_time;
		gettimeofday(&start_time,NULL);
		if (thread->func) thread->func(thread->arg);
		gettimeofday(&end_time,NULL);
		tv_sub(&end_time,&start_time);

		pthread_mutex_lock(&thread->mutex);
		if (0 == t_idle(pool,thread)) {
			pthread_cond_wait(&thread->cond,&thread->mutex);
			pthread_mutex_unlock(&thread->mutex);
		} else {
			pthread_mutex_unlock(&thread->mutex);
			pthread_cond_destroy(&thread->cond);
			pthread_mutex_destroy(&thread->mutex);

			free(thread);
			break;
		}
	} while(0 == pool->is_close);

	/* pool is close or thread is exeception idle_thread failure*/
	pthread_mutex_lock(&pool->mutex);
	pool->thread_hwm_num--;
	if (pool->thread_hwm_num <= 0) pthread_cond_signal(&pool->drop_cond);
	pthread_mutex_unlock(&pool->mutex);

	return NULL;
}

/*let this thread idle ,can be use */
int t_idle(Tp_t *pool,Thread_t *thread)
{
	pthread_mutex_lock(&pool->mutex);
	pool->thread_list[pool->thread_idle_num] = thread;	/*空闲的线程指针放回来*/
	pool->thread_idle_num++;				/*假设有释放后有5个线程，修改的是thread_list[4]*/	

	dump(L_DEBUG, "One Thread is idle %d %lu", pool->thread_idle_num, pthread_self());
	
	pthread_cond_signal(&pool->idle_cond);			/*这个线程可用了，告诉有线程空闲，解决线程不足的等待*/

	if (pool->thread_idle_num >= pool->thread_hwm_num)	/*如果所有线程都idle*/
		pthread_cond_signal(&pool->allidle_cond);
	
	pthread_mutex_unlock(&pool->mutex);

	return 0;
}

/*往线程池里加任务，参数为任务要执行的函数和参数*/

int tp_add(void *_pool,func_pointer fp,void *arg)
{
	//arg can be NULL
	if ((NULL == _pool)||(NULL == fp)) {
		return -1;
	}

	Tp_t		*pool	= (Tp_t*)_pool;
	Thread_t	*thread	= NULL;			/*可用线程*/

	pthread_mutex_lock(&pool->mutex);

	if (pool->thread_idle_num <= 0) {
		/*无空闲线程*/
		if (pool->thread_hwm_num >= pool->thread_max_num) {
			/*无法再创建线程了，到达上限了，只能等待到有空闲线程*/
			dump(L_FAILURE,"Idle thread is none, begin hang...");
			pthread_cond_wait(&pool->idle_cond,&pool->mutex);
		} else {
			pthread_attr_t	attr;
			/*创建线程*/
			if (NULL == (thread = (Thread_t*)malloc(sizeof(Thread_t))))
			{
				dump(L_FAILURE,"error-----\n");
				pthread_mutex_unlock(&pool->mutex);				
				return -1;
			}
			memset(thread,0,sizeof(Thread_t));
			pthread_mutex_init(&thread->mutex,NULL);
			pthread_cond_init(&thread->cond,NULL);
			thread->func = fp;
			thread->arg  = arg;
			thread->parent = pool;

			pthread_attr_init(&attr);
			pthread_attr_setstacksize (&attr, STACKSIZE);

			pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

			if (0 == pthread_create(&thread->id,&attr,t_thread,thread))
			{
				pool->thread_hwm_num++;
				dump(L_DEBUG, "Create thread successfully %d-%d",
					pool->thread_hwm_num, pool->thread_max_num);
				pthread_attr_destroy(&attr);
				pthread_mutex_unlock(&pool->mutex);
				return 0;	
				/* 这里idle不会增加，只有在idle_thread + 1 */
			} else {
				pthread_mutex_destroy(&thread->mutex);
				pthread_cond_destroy(&thread->cond);
				pthread_attr_destroy(&attr);
				free(thread);
				pthread_mutex_unlock(&pool->mutex);
				return -1;
			}
		}
	}

	/*有空闲线程 1 本来就有 2 等了一会有的*/
	/*选择最上面的一个可用线程*/

	pool->thread_idle_num--;				/*假设有5个空闲线程，那么能使用的就是thread_list[4]*/
	thread = pool->thread_list[pool->thread_idle_num];
	pool->thread_list[pool->thread_idle_num] = NULL;	/*取出后，清空，因为不一定放回这个位置*/		

	pthread_mutex_lock(&thread->mutex);	
	thread->func = fp;
	thread->arg = arg;
	thread->parent = pool;

	pthread_cond_signal(&thread->cond);			/*通知该线程,开始干活*/
	pthread_mutex_unlock(&thread->mutex);

	dump(L_SUCCESS,"pool state : idle thread %d allocate thread %d",
		pool->thread_idle_num, pool->thread_hwm_num);
	pthread_mutex_unlock(&pool->mutex);

	return 0;
}

/* drop thread pool*/
void* tp_drop(void *_pool)
{
	if (NULL == _pool) {
		dump(L_FAILURE,"drop threadpool error\n");
		return NULL;
	}

	int		i;
	Tp_t		*pool = (Tp_t *)_pool;
	Thread_t	*thread	= NULL;
	pthread_mutex_lock(&pool->mutex);

	pool->is_close = 1;

	/*wait all thread idle, only idle then signal*/
	if (pool->thread_hwm_num > pool->thread_idle_num) {
		dump(L_SUCCESS,"Wait %d thread idle", 
			pool->thread_hwm_num - pool->thread_idle_num);
		pthread_cond_wait(&pool->allidle_cond, &pool->mutex);
	}
	ASSERT(pool->thread_hwm_num == pool->thread_idle_num);
	
	/* signal all thread, let thread exit */
	for(i = 0; i<pool->thread_hwm_num; i++) {
		thread = pool->thread_list[i];
		pthread_mutex_lock(&thread->mutex);
		pthread_cond_signal(&thread->cond);
		pthread_mutex_unlock(&thread->mutex);
	}
	/* some thread not exit */
	if (pool->thread_hwm_num > 0) {
		dump(L_SUCCESS,"Wait %d thread empty", pool->thread_hwm_num);
		pthread_cond_wait(&pool->drop_cond, &pool->mutex);
	}

	/* now all thread exit */
	ASSERT(pool->thread_hwm_num == 0);

	for(i = 0; i < pool->thread_idle_num; i++) {
		free(pool->thread_list[i]);
		pool->thread_list[i] = NULL;
	}

	pthread_mutex_unlock(&pool->mutex);

	pthread_mutex_destroy(&pool->mutex);
	pthread_cond_destroy(&pool->idle_cond);
	pthread_cond_destroy(&pool->allidle_cond);
	pthread_cond_destroy(&pool->drop_cond);

	free(pool->thread_list);
	pool->thread_list = NULL;
	free(pool);
	
	return NULL;
}

void tv_sub(struct timeval* out,const struct timeval* in)
{
	/*out->tv_usec < in->tv_usec*/
	if((out->tv_usec -= in->tv_usec) < 0) {
		/*negative -> postive 1,000,000 = 1s*/			
		(out->tv_sec)--;
		out->tv_usec+=1000000;
	}
	out->tv_sec-=in->tv_sec;
}

/* only use init thread */
void init_func(void* arg)
{
	sleep(2);
}

