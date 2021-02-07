#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cacti.h"

// A sample program which calculates the sum of each row of a matrix, using the system
// of actors. The expected input is as follows: first the number of rows (n), then
// the number of columns (k), then the consecutive values of matrix cells, left-to-right,
// top-to-bottom. Each value is an integer and is followed by the number of miliseconds
// that the program waits for while adding the value a given cell. Thus:
// 2 3 1 2 1 5 12 4 23 9 3 11 7 2
// represents this matrix:
// | 1  1  12 |
// |23  3   7 |
// and the program will wait 4 miliseconds while adding 12 to the value of the first row.
// The expected output, therefore, is:
// 14
// 33

int n;
int k;
int* matrix_data;
int created_actors;

typedef struct actor_state{
    int my_column_number;
    int processed_rows_no;
    int* obtained_values;
    actor_id_t father;
} matrix_actor_state;

typedef struct {
    int prefix;
    int row_number;
} matrix_message;

matrix_actor_state* new_actor_state(int my_column_number, actor_id_t father){
    matrix_actor_state* res = malloc(sizeof(matrix_actor_state));
    res->my_column_number = my_column_number;
    res->processed_rows_no = 0;
    res->father = father;
    if(my_column_number != n){
        res->obtained_values = NULL;
    }
    else{
        res->obtained_values = malloc(k*sizeof(int));
        for(int i = 0; i < k; i++) res->obtained_values[i] = 0;
    }
    return res;
}

matrix_message* new_matrix_message(int pref, int row){
    matrix_message* res = malloc(sizeof(matrix_message));
    res->prefix = pref;
    res->row_number = row;
    return res;
}

void hello(void **stateptr, size_t nbytes, void* data);

void forward_matrix(void **stateptr, size_t nbytes, void* data);

void receive_hello_response(void **stateptr, size_t nbytes, void* data);

act_t h = &hello;
act_t f = &receive_hello_response;
act_t s = &forward_matrix;

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
    res.message_type = 1;
    res.data = NULL;
    res.nbytes = 0;
    return res;
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

void sleep_mili(int row_no, int col_no){
    usleep(matrix_data[2*n*row_no+2*col_no+1] * 1000);
}

int value_row_column(int row_no, int col_no){
    int res = matrix_data[2*n*row_no+2*col_no];
    return res;
}

void hello(void **stateptr, size_t nbytes, void* data) {
    (void) data;
    (void) nbytes;
    if(created_actors == 0){
        send_message(actor_id_self(), new_spawn_message());
        ++created_actors;
    }
    else{
        matrix_actor_state* mas = new_actor_state(created_actors, created_actors-1);
        *stateptr = mas;
        if(created_actors == n){
            send_message(created_actors - 1, new_hello_response());
        }
        else{
            send_message(actor_id_self(), new_spawn_message());
        }
        ++created_actors;
        free(data);
    }
}

// Receives a message to calculate the value of a specific row,
// and sends the same type of message further if necessary
void forward_matrix(void **stateptr, size_t nbytes, void* data){
    (void) nbytes;
    matrix_actor_state* state = *((matrix_actor_state**)stateptr);
    matrix_message received = *((matrix_message*) data);
    sleep_mili(received.row_number, state->my_column_number-1);
    state->processed_rows_no += 1;
    if(state->my_column_number == n){
        state->obtained_values[received.row_number] += value_row_column(received.row_number, n-1) +
                                                        received.prefix;
        free(data);
        if(state->processed_rows_no == k){
            for(int i = 0; i < k; i++){
                printf("%d\n", state->obtained_values[i]);
            }
            free(state->obtained_values);
            message_t mes;
            mes.message_type = MSG_GODIE;
            send_message(0, mes);
        }
    }
    else{
        matrix_message* new_message_data = new_matrix_message(received.prefix +
                value_row_column(received.row_number, state->my_column_number-1), received.row_number);
        message_t new_message;
        new_message.message_type = 2;
        new_message.data = new_message_data;
        new_message.nbytes = sizeof(matrix_message);
        free(data);
        send_message(state->my_column_number+1, new_message);
    }
    if(state->processed_rows_no == k){
        free(state);
        commit_suicide();
    }
}

void receive_hello_response(void **stateptr, size_t nbytes, void* data){
    (void) data;
    (void) nbytes;
    actor_id_t se = actor_id_self();
    if(se == 0){
        matrix_message* matr_message;
        message_t message;
        for(int i = 0; i < k; i++){
            matr_message = new_matrix_message(0, i);
            message.message_type = 2;
            message.nbytes = sizeof(matrix_message);
            message.data = matr_message;
            send_message(1, message);
        }
    }
    else{
        matrix_actor_state* mas = (matrix_actor_state*) *stateptr;
        send_message(mas->father, new_hello_response());
    }
}

// First, all the actors are created: upon receiving 'hello', every actor spawns a new actor and
// expects a message of type 1 from the child - save for the one responsible for the last column,
// which does not spawn any new actors, and sends a 1 to its 'father'. Upon receiving a 1, every
// actor sends it back to its respective father - except, again, for the first one, which instead
// initializes the calculation by sending k messages of type 2 to the actor responsible for the
// first column. Upon receiving a message of type 2, the actor performs a calculation pertaining
// to its column and the row specified in the message and resends the message further. The last
// actor outputs the obtained sums after having obtained all of them. Each actor commits 'suicide'
// by sending a GODIE message to itself after it's done with its calculations.
// The main function sends 'hello' to the first actor (not responsible for any column, but for
// a proper start of the calculation).
int main(){
    created_actors = 0;
    scanf("%d %d", &k, &n);
    matrix_data = malloc(2*n*k*sizeof(int));
    for(int i = 0; i < 2*n*k; i++){
        scanf("%d", matrix_data + i);
    }
    actor_id_t dir;

    actor_system_create(&dir, new_role());
    message_t message;
    message.message_type = 0;
    send_message(dir, message);
    actor_system_join(dir);

    free(matrix_data);
	return 0;
}
