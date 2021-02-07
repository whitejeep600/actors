
#ifndef CACTI_DATA_STRUCTURES_H
#define CACTI_DATA_STRUCTURES_H

#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

#include "cacti.h"

// A global queue of actors waiting to be executed by threads,
// implemented as a cyclic buffer with dynamic size.
typedef struct message_queue_s{
    pthread_cond_t* actor_cond;
    pthread_mutex_t* mutex;
    actor_id_t * messages;
    int size;
    int start;
    int occupied;
    bool full;
} message_queue;

// A blocking queue implemented as a cyclic buffer with a set size,
// storing messages sent to a given actor.
typedef struct blocking_queue_s{
    pthread_mutex_t* mutex;
    size_t size;
    message_t* buffer;
    size_t start;
    size_t end;
    bool full;
} blocking_queue;

typedef struct actor_info_s{
    actor_id_t actor_id;
    void* stateptr;
    pthread_mutex_t* mutex;
    bool dead;
    bool waiting; // for a working thread to take care of its current task
    role_t* role;
    blocking_queue* messages;
} actor_info;

typedef struct actors_vector_s{
    pthread_mutex_t* mutex;
    actor_id_t total;
    actor_id_t occupied;
    actor_info** actors;
} actors_vector;

typedef struct global_data_s{
    actor_id_t num_of_actors;
    actor_id_t alive_actors;
    actors_vector* actors;
    message_queue* message_q;
    pthread_mutex_t* mutex;
    bool started;
    bool finished;
    pthread_cond_t* started_cond;
    pthread_cond_t* thread_join_cond;
    pthread_cond_t* sigint_cond;
    pthread_t* thread_descriptors;
    actor_id_t* thread_to_actor; // maps thread IDs to the currently executed actors. Sorted, and binsearched if needed.
    int finished_threads;
    pthread_t director_id;
} global_data_t;

actor_info* new_actor_info(void *const role, actor_id_t actor_id);

void destroy_system(global_data_t* global);

void initialize_global_data(global_data_t* global_data);

message_t bl_queue_pop(blocking_queue* bq);

// Fails if the queue's cyclic buffer is full.
bool bl_queue_push(blocking_queue* bq, message_t new_el);

bool bl_queue_empty(blocking_queue* bq);

void actors_vector_insert(actors_vector* av, actor_info* ai);

void message_queue_push(message_queue* mq, actor_id_t new_el);

actor_id_t message_queue_pop(global_data_t* global_data);

bool message_queue_empty(message_queue* mq);

message_t new_message(message_type_t mes_type, size_t mes_size, void* mes_data);

sigset_t new_sigint_set();

void sort_descriptors(pthread_t* thread_descriptors);


#endif //CACTI_DATA_STRUCTURES_H
