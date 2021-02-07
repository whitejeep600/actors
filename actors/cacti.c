#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

#include "cacti.h"
#include "data_structures.h"

global_data_t global_data;

// Returns the index of the current thread in the map (implemented as two arrays of fixed size)
// connecting thread IDs to the currently executed actors. The index is found by binary search.
int current_thread_index(){
    pthread_t* threads = global_data.thread_descriptors;
    pthread_t current = pthread_self();
    int l = 0;
    int p = POOL_SIZE-1;
    int s;
    while(l < p){
        s = (l+p) / 2;
        if(threads[s] < current){
            l = s+1;
        }
        else if(threads[s] > current){
            p = s;
        }
        else{
            return s;
        }
    }
    return l;
}

void set_executed_actor(actor_id_t ait){
    global_data.thread_to_actor[current_thread_index()] = ait;
}

bool actor_exists(actor_id_t ait){
    return ait < global_data.num_of_actors;
}

bool actor_dead(actor_id_t ait){
    bool res = global_data.actors->actors[ait]->dead;
    return res;
}

void set_waiting(actor_info* af, bool to_set){
    pthread_mutex_lock(af->mutex);
    af->waiting = to_set;
    pthread_mutex_unlock(af->mutex);
}

void kill_actor(actor_info* af){
    pthread_mutex_lock(af->mutex);
    af->dead = true;
    pthread_mutex_unlock(af->mutex);
    pthread_mutex_lock(global_data.mutex);
    global_data.alive_actors -= 1;
    pthread_mutex_unlock(global_data.mutex);
}

// If an actor, whose message queue is not empty, is not present in the queue of actors waiting
// for execution, it will join that queue here.
void join_queue(actor_info* current_actor){
    pthread_mutex_lock(global_data.message_q->mutex);
    pthread_mutex_lock(current_actor->mutex);
    if((!current_actor->waiting || message_queue_empty(global_data.message_q)) &&
            !bl_queue_empty(current_actor->messages)){
        current_actor->waiting = true;
        message_queue_push(global_data.message_q, current_actor->actor_id);
        pthread_cond_signal(global_data.message_q->actor_cond);
    }
    pthread_mutex_unlock(current_actor->mutex);
    pthread_mutex_unlock(global_data.message_q->mutex);
}

int send_message(actor_id_t actor, message_t message){
    if(!actor_exists(actor)){
        return -2;
    }
    if(global_data.finished || actor_dead(actor)){
        return -1;
    }
    actor_info* current_actor = global_data.actors->actors[actor];
    bool sent = bl_queue_push(current_actor->messages, message);
    if(!sent) return -3;
    join_queue(current_actor);
    return 0;
}

actor_id_t actor_id_self(){
    return global_data.thread_to_actor[current_thread_index()];
}

void spawn_actor(role_t* role){
    if(global_data.finished) return;
    actor_id_t new_actor_id;
    actor_info* new_actor = new_actor_info(role, global_data.num_of_actors);
    actors_vector_insert(global_data.actors, new_actor);
    new_actor_id = (global_data.num_of_actors)++;
    actor_id_t* makers_id = malloc(sizeof(actor_id_t));
    *makers_id = actor_id_self();
    global_data.alive_actors += 1;
    send_message(new_actor_id, new_message(MSG_HELLO, sizeof(actor_id_t*), (void*) makers_id));
}

void sync_start_thread(){
    pthread_mutex_lock(global_data.mutex);
    while(!global_data.started){
        pthread_cond_wait(global_data.started_cond, global_data.mutex);
    }
    pthread_mutex_unlock(global_data.mutex);
}

void enable_start(){
    pthread_mutex_lock(global_data.mutex);
    global_data.started = true;
    for(int i = 0; i < POOL_SIZE; ++i){
        pthread_cond_signal(global_data.started_cond);
    }
    pthread_mutex_unlock(global_data.mutex);
}

bool valid_order(message_type_t current_order, role_t* current_role){
    return current_order == MSG_SPAWN || current_order == MSG_GODIE ||
           current_order == MSG_HELLO || (size_t) current_order < current_role->nprompts;
}

bool can_enter_loop(actor_info** current_actor, message_t* message) {
    message_queue* mq = global_data.message_q;
    pthread_mutex_lock(mq->mutex);
    bool res;
    actor_id_t popped;
    while(message_queue_empty(mq) && !global_data.finished && global_data.alive_actors > 0){
        pthread_cond_wait(mq->actor_cond, mq->mutex);
    }
    if(global_data.finished){
        res = false;
    }
    else if(global_data.alive_actors == 0){
        pthread_kill(global_data.director_id, SIGINT);
        pthread_cond_wait(global_data.message_q->actor_cond, mq->mutex);
        res = false;
    }
    else{
        popped = message_queue_pop(&global_data);
        *current_actor = global_data.actors->actors[popped];
        set_waiting(*current_actor, false);
        *message = bl_queue_pop((*current_actor)->messages);
        res = true;
    }
    pthread_mutex_unlock(mq->mutex);
    return res;
}

void* working_thread() {
    sync_start_thread();
    message_t current_message;
    role_t* current_role;
    actor_info* current_actor;
    void** stateptr;
    message_type_t current_message_type;
    while(can_enter_loop(&current_actor, &current_message)){
        set_executed_actor(current_actor->actor_id);
        current_role = current_actor->role;
        current_message_type = current_message.message_type;
        if(!valid_order(current_message_type, current_role)){
            fprintf(stderr, "Warning: trying to access a non-existent actor function\n");
            continue;
        }
        if(current_message_type == MSG_GODIE){
            kill_actor(current_actor);
            join_queue(current_actor);
        }
        else{
            if(current_message_type == MSG_SPAWN){
                spawn_actor((role_t*) current_message.data);
            }
            else{
                stateptr = &(current_actor->stateptr);
                (current_role->prompts[current_message_type])(stateptr, current_message.nbytes, current_message.data);
            }
            join_queue(current_actor);
        }
    }
    pthread_mutex_lock(global_data.mutex);
    global_data.finished_threads += 1;
    if(global_data.finished_threads == POOL_SIZE){
        pthread_cond_signal(global_data.thread_join_cond);
    }
    pthread_mutex_unlock(global_data.mutex);
    return NULL;
}

// Waiting for the working threads to finish after instructing them to do so.
void director_join(){
    pthread_mutex_lock(global_data.mutex);
    global_data.finished = true;
    pthread_mutex_lock(global_data.message_q->mutex);
    for(int i = 0; i < POOL_SIZE; i++){
        pthread_cond_signal(global_data.message_q->actor_cond);
    }
    pthread_mutex_unlock(global_data.message_q->mutex);
    while(global_data.finished_threads < POOL_SIZE){
        pthread_cond_wait(global_data.thread_join_cond, global_data.mutex);
    }
    pthread_mutex_unlock(global_data.mutex);
}

// A 'director' thread responsible for a synchronized start of the working threads,
// the cleanup, and receiving an externally-sent SIGINT (if necessary).
void* director(){
    global_data.actors->occupied = 1;
    enable_start();

    // Waiting for a SIGINT, sent either from outside the process or from a
    // working thread (which, to simplify the process of ending the program
    // execution, sends it if all the actors are dead).
    sigset_t sigint_set = new_sigint_set();
    int sig;
    sigwait(&sigint_set, &sig);

    director_join();
    destroy_system(&global_data);
    return NULL;
}

void actor_system_join(actor_id_t actor){
    pthread_mutex_lock(global_data.mutex);
    if(!actor_exists(actor)){
        pthread_mutex_unlock(global_data.mutex);
        fprintf(stderr, "Warning: rejected request to wait for a non-existent actor\n");
    }
    else{
        pthread_mutex_unlock(global_data.mutex);
        pthread_join(global_data.director_id, NULL);
    }
}

int actor_system_create(actor_id_t *actor, role_t *const role){
    pthread_attr_t* default_attributes = malloc(sizeof(pthread_attr_t));
    pthread_attr_init(default_attributes);
    pthread_attr_setdetachstate(default_attributes, PTHREAD_CREATE_JOINABLE);
    pthread_t* thread_descriptors = malloc(POOL_SIZE * sizeof(pthread_t));
    sigset_t sigint_set = new_sigint_set();
    pthread_sigmask(SIG_BLOCK, &sigint_set, NULL);
    pthread_t* temp_desc = malloc(sizeof(pthread_t));
    initialize_global_data(&global_data);
    actor_info* info = new_actor_info(role, 0);
    global_data.actors->actors[0] = info;
    int err;
    global_data.thread_descriptors = thread_descriptors;
    for(int i = 0; i < POOL_SIZE; ++i){
        err = pthread_create(temp_desc, default_attributes, &working_thread, NULL);
        thread_descriptors[i] = *temp_desc;
        pthread_detach(*temp_desc);
        if(err != 0) return err;
    }
    sort_descriptors(thread_descriptors);
    free(temp_desc);
    pthread_t director_id;
    err = pthread_create(&director_id, default_attributes, &director, NULL);
    if(err != 0){
        destroy_system(&global_data);
    }
    *actor = 0;
    free(default_attributes);
    global_data.director_id = director_id;
    return err;
}
