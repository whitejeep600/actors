#include <stdio.h>
#include <stdlib.h>

#include "cacti.h"

// A sample program which calculates the factorial of a number using a system of actors.
// To execute, a command like "echo 10 | ./factorial" can be used.

typedef struct{
    int n;
    int k;
    unsigned long long par_fac;
}factorial_data;

void hello(void **stateptr, size_t nbytes, void* data);

void forward_factorial(void **stateptr, size_t nbytes, void* data);

void receive_hello_response(void **stateptr, size_t nbytes, void* data);

act_t h = &hello;
act_t f = &forward_factorial;
act_t s = &receive_hello_response;

role_t* new_role(){
    role_t* res = malloc(sizeof(role_t));
    res->nprompts = 3;
    void** prompts = malloc(3 * sizeof(act_t));
    prompts[0] = h;
    prompts[1] = f;
    prompts[2] = s;
    res->prompts = (act_t*) prompts;
    return res;
}

message_t new_spawn_message(){
    message_t res;
    res.message_type = MSG_SPAWN;
    res.data = new_role();
    res.nbytes = sizeof(res.data);
    return res;
}

message_t new_hello_response(){
    message_t res;
    res.message_type = 2;
    actor_id_t* ait = malloc(sizeof(actor_id_t));
    *ait = actor_id_self();
    res.data = ait;
    res.nbytes = sizeof(res.data);
    return res;
}

void hello(void **stateptr, size_t nbytes, void* data) {
    (void) stateptr;
    (void) &nbytes;
    actor_id_t makers_id =  *((actor_id_t* )data);
    free(data);
    send_message(makers_id, new_hello_response());
}

message_t new_suicide(){
    message_t suicide;
    suicide.data = NULL;
    suicide.nbytes = 0;
    suicide.message_type = MSG_GODIE;
    return suicide;
}

void commit_suicide(){
    send_message(actor_id_self(), new_suicide());
}

// Forwards the order to calculate a partial factorial.
void forward_factorial(void **stateptr, size_t nbytes, void* data){
    (void) &nbytes;
    *stateptr = data;
    factorial_data* data_cast = (factorial_data*) data;
    if(data_cast->k == data_cast->n){
        printf("%llu\n", data_cast->par_fac);
        commit_suicide();
    }
    else{
        send_message(actor_id_self(), new_spawn_message());
    }
}

void receive_hello_response(void **stateptr, size_t nbytes, void* data){
    (void) &nbytes;
    actor_id_t sender = *((actor_id_t*) data);
    factorial_data* cast_state = *((factorial_data**) stateptr);
    cast_state->k += 1;
    cast_state->par_fac *= cast_state->k;
    message_t message;
    message.data = (void*) cast_state;
    message.nbytes = sizeof(message.data);
    message.message_type = 1;
    send_message(sender, message);
    commit_suicide();
    free(data);
}

// Each actor, after receiving a message of type 1, creates a new actor, sends it a hello message,
// waits for its response (a message of type 2), then calculates its partial factorial and sends
// it to the child in a message of type 1. The main function sends a message of type 1 to the first
// actor. The last actor does not create any children, but outputs the result. Each actor, after
// it's done with its task, commits 'suicide' by sending a GODIE message to itself.
int main(){
    int n;
    scanf("%d", &n);
    factorial_data fm;
    fm.k = 1;
    fm.n = n;
    fm.par_fac = 1;
    actor_id_t dir;
    actor_system_create(&dir, new_role());
    message_t message;
    message.message_type = 1;
    message.nbytes = sizeof(factorial_data*);
    message.data = (void*) &fm;
    send_message(dir, message);
    actor_system_join(dir);
	return 0;
}
