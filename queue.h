#include <stdlib.h>
#include <string.h>

typedef struct Queue {
    int front, rear, size;
    unsigned capacity;
    char **links;
} Queue;

Queue *init_queue(unsigned capacity) {
    Queue *queue = (Queue *)malloc(sizeof(Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->links = malloc(queue->capacity * sizeof(char *));

    return queue;
}

int isFull(Queue *queue) { return queue->size == queue->capacity; }

int isEmpty(Queue *queue) { return queue->size == 0; }

void enqueue(Queue *queue, char *link) {
    if (isFull(queue)) {
        return;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->links[queue->rear] = (char *)malloc(128 * sizeof(char));
    strcpy(queue->links[queue->rear], link);
    queue->size++;
}

char *dequeue(Queue *queue) {
    if (isEmpty(queue)) {
        return NULL;
    }

    char *d = queue->links[queue->front];
    char *link = (char *)malloc(strlen(d) * sizeof(char));
    strcpy(link, d);
    free(d);
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;

    return link;
}