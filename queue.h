#ifndef _QUEUE_H_
#define _QUEUE_H_

#define QUEUE_SIZE 20000

typedef void (*fp) (void *);

void* q_init(size_t size, void *pool);

int q_add(void *q, void *func, void *arg);

void q_print(void *q, fp p);

void print_pointer(void *data);

void q_drop(void *q);

int q_isempty(void *q);

#endif 

