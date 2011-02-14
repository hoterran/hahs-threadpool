#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "common.h"
#include "threadpool.h"
#include "utils.h"
#include "log.h"
#include "queue.h"

typedef struct _Queue_node{
	void	*func;
	void	*arg;
} Q_node;

typedef struct _Queue{
	Q_node 		*first;
	Q_node 		*last;
	size_t		size;
	Q_node		*head;
	void		*pool;
	pthread_mutex_t mutex;
	pthread_cond_t  has_data_cond;
	pthread_cond_t	drop_cond;
	char		is_close;
	pthread_t	tid;
} Q_t;

void	q_remove(Q_t *q, void** func, void** arg);
int	q_isfull(Q_t *q);
void*	q_thread(void *q);
void	print_p(void *data);
int	q_length(Q_t *q);

void* q_init(size_t size, void *pool)
{
	ASSERT(size > 0);
	ASSERT(pool);

	Q_t *q = (Q_t*)malloc(sizeof(Q_t));
	if (NULL == q) return NULL;

	Q_node *qn = (Q_node*)malloc(size * sizeof(Q_node));

	if (NULL == qn) return NULL;
	q->first = qn;
	q->last = qn;
	q->head = qn;
	q->size = size;
	q->pool = pool;
	q->is_close = 0;
	memset(q->head, 0, size * sizeof(Q_node));

	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->has_data_cond, NULL);
	pthread_cond_init(&q->drop_cond, NULL);

	if (0 != pthread_create(&q->tid, NULL, q_thread, q)) {
		free(q->head);
		q->head = NULL;
		free(q);
		q = NULL;
		dump(L_ERROR, "Create thread q_thread error");
	}

	return (void*)q;
}

int q_add(void *qt, void *func, void *arg)
{
        ASSERT(qt);
	ASSERT(func);
	/*arg possible NULL*/

	Q_t *q = (Q_t*)qt;

	pthread_mutex_lock(&q->mutex);
	if (OK == q_isfull(q)) {
		pthread_mutex_unlock(&q->mutex);	
		return ERROR;
	}
        ASSERT(q->last->func == NULL);

	q->last->func = func;
	q->last->arg = arg;
	q->last++;

        if (q->last == q->first + 1) {
                ASSERT(1 == q_length(q));
                dump(L_DEBUG, "Queue send signal, has data");
                pthread_cond_signal(&q->has_data_cond);
        }

        if (q->last == q->head + q->size)
                q->last = q->head;
	dump(L_DEBUG, "Add Queue Length %d", q_length(q));
	pthread_mutex_unlock(&q->mutex);

	return OK;
}

int q_isempty(void *qt)
{
	Q_t *q = qt;
	ASSERT(q);
        if (q->last == q->first) {
                if (NULL == q->first->func) {
                        dump(L_INFO, "Queue is empty");
                        return OK;
                } 
        }
	return ERROR;
}

int q_isfull(Q_t *q)
{
	ASSERT(q);
        if (q->last == q->first) {
                if (NULL != q->last->func) {
                        dump(L_INFO, "Queue is full");
                        return OK;
                }
        }
	return ERROR;
}

void* q_thread(void *qt)
{
	ASSERT(qt);
	Q_t *q = qt;
	void *func;
	void *arg;
	while(0 == q->is_close) {
	        pthread_mutex_lock(&q->mutex);
        	if (OK == q_isempty(q)) {
			pthread_cond_wait(&q->has_data_cond, &q->mutex);
		}
		/* possible close signal has_data cond */
		if (0 != q->is_close) {
                	pthread_mutex_unlock(&q->mutex);
			break;
		}
		q_remove(q, &func, &arg);
                pthread_mutex_unlock(&q->mutex);
		tp_add(q->pool, func, arg); //unlock first then tp add, let q_add go 
	}
	tp_drop(q->pool);
	q->pool = NULL;
	pthread_mutex_lock(&q->mutex);
	pthread_cond_signal(&q->drop_cond);
	pthread_mutex_unlock(&q->mutex);
	pthread_exit(NULL);
	return NULL;
}

void q_remove(Q_t *q, void** func, void** arg)
{
	ASSERT(q);
	ASSERT(q->first->func); 
	ASSERT(func);
	ASSERT(arg);

	*func = q->first->func;
	*arg = q->first->arg;
	q->first->func = NULL;
	q->first->arg = NULL;
	q->first++;

	if (q->first == q->head + q->size)
		q->first = q->head;
	dump(L_DEBUG, "Remove Queue Length %d", q_length(q));
	//q_print(q, print_p);
}

void q_drop(void *qt)
{
	ASSERT(qt);

	Q_t* q = (Q_t*)qt;
	pthread_mutex_lock(&q->mutex);

	q->is_close = 1;
	/* let q thread not wait */
      	if (OK == q_isempty(q))
		pthread_cond_signal(&q->has_data_cond);	
	pthread_cond_wait(&q->drop_cond, &q->mutex);

	pthread_join(q->tid, NULL);

	pthread_mutex_unlock(&q->mutex);

	pthread_mutex_destroy(&q->mutex);
	pthread_cond_destroy(&q->has_data_cond);
	pthread_cond_destroy(&q->drop_cond);
	free(q->head);
	q->head = NULL;
	free(q);
}

int q_length(Q_t *q)
{
	ASSERT(q);

	if (q->last < q->first) {
		return q->size - (q->first - q->last);
	} else {
		return q->last - q->first;
	}
}
void q_print(void *qt, fp p)
{
	ASSERT(qt);
	ASSERT(p);
	Q_t *q = qt;	
	Q_node* qn;
	int i;

	for (i = 0, qn = q->head; i < q->size; i++, qn++) {
		p(qn->func);
		//p(qn->arg);
	}
}

void print_p(void* data)
{
	if (NULL == data)
		printf("[ ]");
	else 
		printf("[x]");
}

/*
int main()
{
	void* d;
	Q_t *q = q_init(10);
	int z1 = 1;
	int z2 = 2;
	int z3 = 3;
	q_add(q, &z1);
	q_add(q, &z2);
	q_add(q, &z3);

        q_print(q, print_int);

	return 1;
} 
*/
