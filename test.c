#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>

#include "common.h"
#include "threadpool.h"
#include "queue.h"
#include "log.h"

void f1(void *arg) {
	printf("call f1\n");
	sleep(1);	
}

void f2(void *arg) {
	printf("call f2\n");
	sleep(3);
}

int main() 
{
	void *pool = NULL;
	void *q = NULL;

	if (NULL == (pool = tp_init(MAX_POOL, INIT_POOL))) {
		printf("tp_init error");
		return ERROR;
	}
               
        if (NULL == (q = q_init(QUEUE_SIZE, pool))) {
		printf("q_init error");
		return ERROR;	
	}

	int i = 0;
	for (i = 0; i < 100; i++) {
		q_add(q, f1, NULL);
		q_add(q, f2, NULL); 
	}

        while(OK != q_isempty(q)) {
		sleep(0.1);
	}

	q_drop(q);

	return OK;
}

