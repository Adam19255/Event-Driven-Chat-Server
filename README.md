# ChatServer - Multithreaded Chat Server in C

ChatServer is a robust, multithreaded chat server implemented in C. It handles multiple client connections concurrently, broadcasts messages, and manages connection pools efficiently.

## Features

- Multithreaded design using select() for efficient I/O multiplexing
- Support for multiple concurrent client connections
- Non-blocking I/O operations
- Dynamic connection pool management
- Message queuing and broadcasting
- Graceful shutdown on SIGINT (Ctrl+C)
- Uppercase conversion of incoming messages before broadcasting

## Usage

Compile the program: gcc -o chatServer chatServer.c -lpthread

Run the program: ./chatServer <port>

- `<port>`: Port number on which the chat server will listen

## How It Works

1. Initializes a connection pool and sets up a welcome socket
2. Uses select() to monitor file descriptors for read and write operations
3. Accepts new client connections and adds them to the connection pool
4. Reads incoming messages from clients
5. Converts messages to uppercase and queues them for broadcast to other clients
6. Writes queued messages to clients when they're ready to receive
7. Manages connection cleanup and removal

## Key Components

- `conn_pool_t`: Structure to manage the connection pool
- `conn_t`: Structure to represent individual client connections
- `msg_t`: Structure to represent messages in the write queue

## Main Functions

- `initPool`: Initializes the connection pool
- `addConn`: Adds a new connection to the pool
- `removeConn`: Removes a connection from the pool and cleans up resources
- `addMsg`: Adds a message to the write queue of all other connections
- `writeToClient`: Writes queued messages to a client

## Error Handling

The server handles various error conditions, including:

- Socket creation and binding errors
- Connection acceptance errors
- Read and write errors
- Memory allocation errors

## Limitations

- No authentication mechanism
- Basic error handling
- No support for private messaging or chat rooms
- No persistence of chat history

## Dependencies

- Standard C libraries
- POSIX threads (pthread)
