#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>
#include <unistd.h>

#define QUEUE_SIZE 10

// mpicc -o at3 at3.c -lpthread
// mpiexec -n 3 ./at3

typedef struct {
    int p[3];
    int owner; 
} Clock;

typedef struct _Queue {
    Clock queue[QUEUE_SIZE];
    int start, end;
    pthread_mutex_t mutex;
    pthread_cond_t is_full, is_empty;
} Queue;

Queue receiveQueue, sendQueue;
Clock global_clock = { { 0, 0, 0 }, 0 };

Queue create_queue() {
    Queue q;
    q.start = 0;
    q.end = 0;
    pthread_mutex_init(&q.mutex, NULL);
    pthread_cond_init(&q.is_full, NULL);
    pthread_cond_init(&q.is_empty, NULL);
    return q;
}

int add_to_queue(Queue *q, Clock c) {
    pthread_mutex_lock(&q->mutex);
    while ((q->end + 1) % QUEUE_SIZE == q->start % QUEUE_SIZE) {
        pthread_cond_wait(&q->is_full, &q->mutex);
    }
    q->queue[q->end % QUEUE_SIZE] = c;
    q->end++;
    pthread_cond_signal(&q->is_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

Clock remove_from_queue(Queue *q) {
    pthread_mutex_lock(&q->mutex);
    while (q->start == q->end) {
        pthread_cond_wait(&q->is_empty, &q->mutex);
    }
    Clock c = q->queue[q->start % QUEUE_SIZE];
    q->start++;
    pthread_cond_signal(&q->is_full);
    pthread_mutex_unlock(&q->mutex);
    return c;
}

void* receive_thread(void* arg) {
    Clock c;
    while (1) {
        MPI_Recv(&c, sizeof(Clock) / sizeof(int), MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        add_to_queue(&receiveQueue, c);
    }
    return NULL;
}

void* send_thread(void* arg) {
    Clock c;
    while (1) {
        c = remove_from_queue(&sendQueue);
        int target = c.owner;
        c.owner = global_clock.owner;
        MPI_Send(&c, sizeof(Clock) / sizeof(int), MPI_INT, target, 0, MPI_COMM_WORLD);
        printf("Send: %d, Clock: (%d, %d, %d)\n", global_clock.owner, global_clock.p[0], global_clock.p[1], global_clock.p[2]);
    }
    return NULL;
}

void Event() {
    global_clock.p[global_clock.owner]++;
    printf("Event: %d, Clock: (%d, %d, %d)\n", global_clock.owner, global_clock.p[0], global_clock.p[1], global_clock.p[2]);
}

void Send(int target) {
    global_clock.p[global_clock.owner]++;
    Clock c = global_clock;
    c.owner = target;
    add_to_queue(&sendQueue, c);
}

void Receive() {
    Clock c = remove_from_queue(&receiveQueue);
    global_clock.p[global_clock.owner]++;
    for (int i = 0; i < 3; i++) {
        if (global_clock.p[i] < c.p[i]) {
            global_clock.p[i] = c.p[i];
        }
    }
    printf("Receive: %d, Clock: (%d, %d, %d)\n", global_clock.owner, global_clock.p[0], global_clock.p[1], global_clock.p[2]);
}

void process0(){
    Event();
    Send(1);
    Receive();
    Send(2);
    Receive();
    Send(1);
    Event();
}

void process1(){
    Send(0);
    Receive();
    Receive();
}

void process2(){
    Event();
    Send(0);
    Receive();
}

int main(int argc, char **argv) {
    int my_rank;
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    global_clock.owner = my_rank;

    receiveQueue = create_queue();
    sendQueue = create_queue();

    pthread_t threadRecv, threadSend;
    pthread_create(&threadRecv, NULL, receive_thread, NULL);
    pthread_create(&threadSend, NULL, send_thread, NULL);

    switch (my_rank) {
        case 0: process0(); break;
        case 1: process1(); break;
        case 2: process2(); break;
        default:
            printf("Invalid rank %d", my_rank);
            exit(1);
    }

    pthread_join(threadRecv, NULL);
    pthread_join(threadSend, NULL);
    
    MPI_Finalize();
    return 0;
}
