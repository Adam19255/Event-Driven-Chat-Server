#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include "chatServer.h"

static int end_server = 0;
static int welcomeSock;

void intHandler(int SIG_INT) {
    if (SIG_INT == SIGINT) {
        // Set the flag to end the main loop
        end_server = 1;
    }
}

void cleanUp(conn_pool_t *pool){
    while (pool->conn_head != NULL) {
        removeConn(pool->conn_head->fd, pool);
    }
    free(pool);
}

int main (int argc, char *argv[]){
    signal(SIGINT, intHandler);

    if (argc != 2) {
        perror("Usage: server <port>\n");
        exit(1);
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        perror("Usage: server <port>\n");
        exit(1);
    }

    conn_pool_t *pool = malloc(sizeof(conn_pool_t));
    if (initPool(pool) == -1) {
        perror("initPool\n");
        exit(1);
    }

    // This is the welcome socket
    welcomeSock = socket(AF_INET, SOCK_STREAM, 0);
    if (welcomeSock == -1) {
        perror("socket\n");
        cleanUp(pool);
        exit(1);
    }

    // Setting the socket to be nonblocking
    int on = 1;
    if (ioctl(welcomeSock, FIONBIO, (char *)&on) == -1) {
        perror("ioctl\n");
        cleanUp(pool);
        close(welcomeSock);
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(welcomeSock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind\n");
        cleanUp(pool);
        close(welcomeSock);
        exit(1);
    }

    // Set up a socket to listen for incoming connections
    if (listen(welcomeSock, SOMAXCONN) == -1) {
        perror("listen\n");
        cleanUp(pool);
        close(welcomeSock);
        exit(1);
    }

    // Add the welcoming socket to the pool
    addConn(welcomeSock, pool);

    while (!end_server) {
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        int readyCount = 0;

        // Copy the sets
        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;

        // Checking to see who is ready to read or write
        pool->nready = select(pool->maxfd + 1, &pool->ready_read_set, &pool->ready_write_set, NULL, NULL);
        if (pool->nready == -1) {
            continue;
        }

        // One or more descriptors are readable or writable. Need to determine which ones they are.
        for (int i = 0; i <= pool->maxfd; ++i) {
            // Check to see if this descriptor is ready for read
            if (FD_ISSET(i, &pool->ready_read_set)) {
                readyCount++;
                if (i == welcomeSock) {
                    // We are in the welcome socket ready to add a new connection

                    int new_sd = accept(welcomeSock, NULL, NULL);
                    if (new_sd == -1) {
                        perror("accept\n");
                        continue;
                    }
                    printf("New incoming connection on sd %d\n", new_sd);
                    addConn(new_sd, pool);
                } else {
                    printf("Descriptor %d is readable\n", i);
                    // Read from the socket
                    char buffer[BUFFER_SIZE];
                    int len = read(i, buffer, BUFFER_SIZE);

                    // Data was received, add msg to all other connections
                    if (len > 0) {
                        printf("%d bytes received from sd %d\n", len, i);
                        addMsg(i, buffer, len, pool);
                    }
                        // Connection has been closed by client
                    else if (len == 0) {
                        printf("Connection closed for sd %d\n", i);
                        removeConn(i, pool);
                    }
                        // Read was bad
                    else {
                        perror("read\n");
                    }
                }
            } /* End of if (FD_ISSET()) */

            // Check to see if this descriptor is ready for write
            if (FD_ISSET(i, &pool->ready_write_set)) {
                readyCount++;
                writeToClient(i, pool);
            }
            // No need to run the full loop if the sockets that were ready are done
            if (readyCount == pool->nready){
                break;
            }
        }
    }
    cleanUp(pool);

    return 0;
}


int initPool(conn_pool_t* pool) {
    if (pool == NULL) {
        return -1;
    }

    //initialized all fields
    pool->maxfd = -1;
    pool->nready = 0;
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_write_set);
    pool->conn_head = NULL;
    pool->nr_conns = 0;
    return 0;
}

int addConn(int sd, conn_pool_t* pool) {
    // Allocate memory for the new connection
    conn_t *new_conn = (conn_t*)malloc(sizeof(conn_t));
    if (new_conn == NULL) {
        perror("addConn: malloc\n ");
        return -1;
    }

    // Initialize connection fields
    new_conn->fd = sd;
    new_conn->write_msg_head = NULL;
    new_conn->write_msg_tail = NULL;

    // Add connection to the pool
    new_conn->next = pool->conn_head;
    new_conn->prev = NULL;
    // If there are previous connections we update the prev value
    if (pool->conn_head != NULL) {
        pool->conn_head->prev = new_conn;
    }
    pool->conn_head = new_conn;
    pool->nr_conns++;

    // Update maxfd if necessary
    if (sd > pool->maxfd) {
        pool->maxfd = sd;
    }

    // Add the new connection's file descriptor to the read set
    FD_SET(sd, &pool->read_set);

    return 0;
}


int removeConn(int sd, conn_pool_t* pool) {
    if (sd != welcomeSock){
        printf("removing connection with sd %d \n", sd);
    }

    // Find the connection to remove
    conn_t *current = pool->conn_head;
    while (current != NULL && current->fd != sd) {
        current = current->next;
    }

    if (current == NULL) {
        return -1;
    }

    // Store the pointer to the connection to be able to remove later
    conn_t *toRemove = current;

    // Remove the connection from the pool
    if (current->prev != NULL) {
        current->prev->next = current->next;
    } else {
        pool->conn_head = current->next;
    }
    if (current->next != NULL) {
        current->next->prev = current->prev;
    }
    pool->nr_conns--;

    // Remove the connection's file descriptor from the read and write sets
    FD_CLR(sd, &pool->read_set);
    FD_CLR(sd, &pool->write_set);

    // Free memory associated with any messages queued for this connection
    msg_t *msg = current->write_msg_head;
    while (msg != NULL) {
        msg_t *temp = msg;
        msg = msg->next;
        free(temp->message); // Free memory associated with the message
        free(temp); // Free memory associated with the message node
    }
    free(msg);

    // Update maxfd if necessary
    if (pool->maxfd == sd) {
        int maxfd = -1;
        current = pool->conn_head;
        while (current != NULL) {
            if (current->fd > maxfd) {
                maxfd = current->fd;
            }
            current = current->next;
        }
        pool->maxfd = maxfd;
    }

    // Close the connection's file descriptor
    close(sd);

    // Free memory associated with the connection
    free(toRemove);

    return 0;
}

int addMsg(int sd, char* buffer, int len, conn_pool_t* pool) {
    // Iterate through all connections in the pool
    conn_t *current = pool->conn_head;
    while (current != NULL) {
        // Check if the connection is not the origin and add the message
        if (current->fd != sd) {
            // Allocate memory for the message
            char *message = (char*)malloc(len + 1);
            if (message == NULL) {
                perror("addMsg malloc\n");
                return -1;
            }

            // Converting the message into upper case
            for (int i = 0; i < len; i++) {
                message[i] = toupper(buffer[i]);
            }
            message[len] = '\0';

            // Create a new message node
            msg_t *new_msg = (msg_t*)malloc(sizeof(msg_t));
            if (new_msg == NULL) {
                perror("addMsg malloc\n");
                free(message);
                return -1;
            }
            new_msg->message = message;
            new_msg->size = len;
            new_msg->prev = NULL;
            new_msg->next = current->write_msg_head;
            if (current->write_msg_head != NULL) {
                current->write_msg_head->prev = new_msg;
            } else {
                current->write_msg_tail = new_msg;
            }
            current->write_msg_head = new_msg;
            // Add the connection's file descriptor to the write set
            FD_SET(current->fd, &pool->write_set);
        }
        current = current->next;
    }

    return 0;
}

int writeToClient(int sd, conn_pool_t* pool) {
    // Find the connection
    conn_t *current = pool->conn_head;
    while (current != NULL && current->fd != sd) {
        current = current->next;
    }

    if (current == NULL) {
        return -1;
    }

    // Write all messages in the queue to the client
    msg_t *current_msg = current->write_msg_head;
    while (current_msg != NULL) {
        int end_write = 0;
        while (end_write < current_msg->size){
            // Write and update the message buffer and size to account for the remaining unwritten data
            int bytes_written = write(sd, current_msg->message + end_write, current_msg->size - end_write);
            if (bytes_written == -1) {
                perror("writeToClient: write\n");
                break;
            }
            end_write += bytes_written;
        }
        // Remove the written message from the queue
        msg_t *temp = current_msg;
        current_msg = current_msg->next;
        free(temp->message);
        free(temp);
    }
    current->write_msg_head = NULL; // Update the write_msg_head to point to the remaining messages
    current->write_msg_tail = NULL;// If all messages are written, update write_msg_tail to NULL

    // Remove the connection's file descriptor from the write set
    FD_CLR(sd, &pool->write_set);

    return 0;
}