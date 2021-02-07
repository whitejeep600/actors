#include "data_structures.h"
#include "cacti.h"

// This function is NOT mutex-guarded and shall only be called
// by the director thread.
void destroy_blocking_queue(blocking_queue* bq){
    pthread_mutex_destroy(bq->mutex);
    free(bq->mutex);
    free(bq->buffer);
    free(bq);
}

void destroy_role(role_t* role){
    if(role == NULL) return;
    free((void*)role->prompts);
    free(role);
}

void destroy_actor_info(actor_info* ai){
    pthread_mutex_destroy(ai->mutex);
    destroy_blocking_queue(ai->messages);
    free(ai->mutex);
    destroy_role(ai->role);
    free(ai);
}

void destroy_actors(actors_vector* actors){
    for(int i = 0; i < actors->occupied; i++) destroy_actor_info(actors->actors[i]);
    pthread_mutex_destroy(actors->mutex);
    free(actors->mutex);
    free(actors->actors);
    free(actors);
}

void destroy_message_queue(message_queue* message_q){
    pthread_mutex_destroy(message_q->mutex);
    pthread_cond_destroy(message_q->actor_cond);
    free(message_q->messages);
    free(message_q->mutex);
    free(message_q->actor_cond);
    free(message_q);
}

void destroy_mutex(pthread_mutex_t* mutex){
    pthread_mutex_destroy(mutex);
}

void destroy_system(global_data_t* global){
    destroy_actors(global->actors);
    destroy_message_queue(global->message_q);
    destroy_mutex(global->mutex);
    pthread_cond_destroy(global->started_cond);
    pthread_cond_destroy(global->thread_join_cond);
    pthread_cond_destroy(global->sigint_cond);
    free(global->thread_descriptors);
    free(global->thread_to_actor);
}

pthread_mutex_t* new_mutex(){
    pthread_mutexattr_t default_mutex_attributes;
    pthread_mutexattr_init(&default_mutex_attributes);
    pthread_mutex_t* result = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(result, &default_mutex_attributes);
    return result;
}

pthread_cond_t* new_cond(){
    pthread_cond_t* result = malloc(sizeof(pthread_cond_t));
    pthread_cond_init(result, NULL);
    return result;
}

blocking_queue* new_blocking_queue(size_t size){
    blocking_queue* res = malloc(sizeof(blocking_queue));
    res->mutex = new_mutex();
    res->size = size;
    res->buffer = malloc(size * sizeof(message_t));
    res->start = 0;
    res->end = 0;
    res->full = false;
    return res;
}

actor_info* new_actor_info(void *const role, actor_id_t actor_id){
    actor_info* res = malloc(sizeof(actor_info));
    res->actor_id = actor_id;
    res->mutex = new_mutex();
    res->stateptr = NULL;
    res->dead = false;
    res->waiting = false;
    res->role = role;
    res->messages = new_blocking_queue(ACTOR_QUEUE_LIMIT);
    return res;
}

actors_vector* new_actors_vector(){
    actors_vector* res = malloc(sizeof(actors_vector));
    res->mutex = new_mutex();
    res->total = 1;
    res->occupied = 0;
    res->actors = malloc(sizeof(actor_info*));
    return res;
}

message_queue* new_message_queue(){
    message_queue* res = malloc(sizeof(message_queue));
    res->actor_cond = new_cond();
    res->mutex = new_mutex();
    res->messages = malloc(sizeof(actor_id_t));
    res->size = 1;
    res->occupied = 0;
    res->start = 0;
    res->full = false;
    return res;
}

void initialize_global_data(global_data_t* global_data){
    global_data->num_of_actors = 1; // the director
    global_data->alive_actors = 1;
    global_data->actors = new_actors_vector();
    global_data->message_q = new_message_queue();
    global_data->mutex = new_mutex();
    global_data->started = false;
    global_data->finished = false;
    global_data->thread_to_actor = malloc(POOL_SIZE * sizeof(actor_id_t));
    global_data->thread_join_cond = new_cond();
    global_data->started_cond = new_cond();
    global_data->sigint_cond = new_cond();
    global_data->finished_threads = 0;
}

message_t bl_queue_pop(blocking_queue* bq){
    pthread_mutex_lock(bq->mutex);
    message_t res = bq->buffer[bq->start];
    bq->full = false;
    bq->start = (bq->start + 1) % (bq->size);
    pthread_mutex_unlock(bq->mutex);
    return res;
}

bool bl_queue_push(blocking_queue* bq, message_t new_el){
    pthread_mutex_lock(bq->mutex);
    if(bq->full){
        pthread_mutex_unlock(bq->mutex);
        return false;
    }
    bq->buffer[bq->end] = new_el;
    bq->end = (bq->end + 1) % (bq->size);
    if(bq->start == bq->end) bq->full = true;
    pthread_mutex_unlock(bq->mutex);
    return true;
}

bool bl_queue_empty(blocking_queue* bq){
    pthread_mutex_lock(bq->mutex);
    bool res = (bq->end == bq->start) && (! bq->full);
    pthread_mutex_unlock(bq->mutex);
    return res;
}

void actors_vector_insert(actors_vector* av, actor_info* ai){
    pthread_mutex_lock(av->mutex);
    if(av->total == av->occupied){
        av->actors = realloc(av->actors, 2 * av->total * sizeof(actor_info*));
        av->total *= 2;
    }
    av->actors[av->occupied] = ai;
    av->occupied += 1;
    pthread_mutex_unlock(av->mutex);
}

void message_queue_push(message_queue* mq , actor_id_t new_el){
    if(mq->full){
        actor_id_t* new_messages = malloc(2*mq->size * sizeof(actor_id_t));
        for(int i = 0; i < mq->size; i++){
            new_messages[i] = mq->messages[(mq->start + i) % mq->size];
        }
        new_messages[mq->size] = new_el;
        mq->start = 0;
        mq->size *= 2;
        mq->occupied += 1;
        if(mq->size != mq->occupied) mq->full = false;
        free(mq->messages);
        mq->messages = new_messages;
    }
    else{
        mq->messages[(mq->start + mq->occupied) % mq->size] = new_el;
        mq->occupied += 1;
        if(mq->size == mq->occupied) mq->full = true;
    }
    pthread_cond_signal(mq->actor_cond);
}

actor_id_t message_queue_pop(global_data_t* global_data){
    message_queue* mq = global_data->message_q;
    if(global_data->finished){
        return 0;
    }
    actor_id_t res = mq->messages[mq->start];
    mq->start += 1;
    mq->start %= mq->size;
    mq->occupied -= 1;
    if(mq->size != mq->occupied) mq->full = false;
    return res;
}

bool message_queue_empty(message_queue* mq){
    return mq->occupied == 0;
}

message_t new_message(message_type_t mes_type, size_t mes_size, void* mes_data){
    message_t res;
    res.message_type = mes_type;
    res.nbytes = mes_size;
    res.data = mes_data;
    return res;
}


sigset_t new_sigint_set(){
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    return set;
}

void sort_descriptors(pthread_t* thread_descriptors){
    pthread_t temp;
    for(int i = 0; i < POOL_SIZE; i++){
        for(int j = 0; j < POOL_SIZE-i-1; j++){
            if(thread_descriptors[j] > thread_descriptors[j+1]){
                temp = thread_descriptors[j];
                thread_descriptors[j] = thread_descriptors[j+1];
                thread_descriptors[j+1] = temp;
            }
        }
    }
}
