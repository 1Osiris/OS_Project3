#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define MAX_ROOMS 50
#define MAX_USERNAME 32

// Data structures
typedef struct User {
    int socket;
    char username[MAX_USERNAME];
    struct User* next;
} User;

typedef struct Room {
    char name[MAX_USERNAME];
    User* users;  // List of users in the room
    struct Room* next;
} Room;

typedef struct DirectMessage {
    User* user1;
    User* user2;
    struct DirectMessage* next;
} DirectMessage;

// Global variables
User* users = NULL;
Room* rooms = NULL;
DirectMessage* direct_messages = NULL;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t dm_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void* handle_client(void* socket_desc);
User* add_user(int socket, const char* username);
void remove_user(int socket);
Room* create_room(const char* room_name);
void join_room(User* user, Room* room);
void leave_room(User* user, Room* room);
void broadcast_room(Room* room, const char* message, User* sender);
void handle_direct_message(User* sender, User* receiver, const char* message);
User* find_user_by_name(const char* username);
Room* find_room_by_name(const char* room_name);

// Main server function
int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8888);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Chat server running on port 8888...\n");
    
    // Create default Lobby room
    create_room("Lobby");
    
    // Accept and handle client connections
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Create thread for new client
        pthread_t thread_id;
        int* new_sock = malloc(sizeof(int));
        *new_sock = client_socket;
        
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    return 0;
}

// Handle individual client connections
void* handle_client(void* socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    char buffer[BUFFER_SIZE];
    User* current_user = NULL;
    
    // Add user as GUEST initially
    char guest_name[MAX_USERNAME];
    snprintf(guest_name, MAX_USERNAME, "GUEST_%d", sock);
    current_user = add_user(sock, guest_name);
    
    // Join Lobby
    Room* lobby = find_room_by_name("Lobby");
    if (lobby) {
        join_room(current_user, lobby);
    }
    
    while (1) {
        int read_size = recv(sock, buffer, BUFFER_SIZE, 0);
        if (read_size <= 0) {
            break;
        }
        buffer[read_size] = '\0';
        
        char command[BUFFER_SIZE];
        char arg[BUFFER_SIZE];
        sscanf(buffer, "%s %s", command, arg);
        
        if (strcmp(command, "login") == 0) {
            pthread_mutex_lock(&users_mutex);
            strncpy(current_user->username, arg, MAX_USERNAME - 1);
            pthread_mutex_unlock(&users_mutex);
            
            char msg[BUFFER_SIZE];
            snprintf(msg, BUFFER_SIZE, "User %s logged in\n", arg);
            broadcast_room(lobby, msg, current_user);
        }
        else if (strcmp(command, "create") == 0) {
            Room* new_room = create_room(arg);
            if (new_room) {
                join_room(current_user, new_room);
            }
        }
        else if (strcmp(command, "join") == 0) {
            Room* room = find_room_by_name(arg);
            if (room) {
                join_room(current_user, room);
            }
        }
        else if (strcmp(command, "leave") == 0) {
            Room* room = find_room_by_name(arg);
            if (room) {
                leave_room(current_user, room);
            }
        }
        else if (strcmp(command, "exit") == 0 || strcmp(command, "logout") == 0) {
            break;
        }
        else {
            // Treat as message to current room
            broadcast_room(lobby, buffer, current_user);
        }
    }
    
    // Cleanup
    remove_user(sock);
    close(sock);
    return NULL;
}

// User management functions
User* add_user(int socket, const char* username) {
    pthread_mutex_lock(&users_mutex);
    
    User* new_user = malloc(sizeof(User));
    new_user->socket = socket;
    strncpy(new_user->username, username, MAX_USERNAME - 1);
    new_user->next = users;
    users = new_user;
    
    pthread_mutex_unlock(&users_mutex);
    return new_user;
}

void remove_user(int socket) {
    pthread_mutex_lock(&users_mutex);
    
    User* current = users;
    User* prev = NULL;
    
    while (current != NULL) {
        if (current->socket == socket) {
            if (prev == NULL) {
                users = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&users_mutex);
}

// Room management functions
Room* create_room(const char* room_name) {
    pthread_mutex_lock(&rooms_mutex);
    
    Room* new_room = malloc(sizeof(Room));
    strncpy(new_room->name, room_name, MAX_USERNAME - 1);
    new_room->users = NULL;
    new_room->next = rooms;
    rooms = new_room;
    
    pthread_mutex_unlock(&rooms_mutex);
    return new_room;
}

void join_room(User* user, Room* room) {
    pthread_mutex_lock(&rooms_mutex);
    
    User* new_user = malloc(sizeof(User));
    *new_user = *user;
    new_user->next = room->users;
    room->users = new_user;
    
    pthread_mutex_unlock(&rooms_mutex);
    
    char msg[BUFFER_SIZE];
    snprintf(msg, BUFFER_SIZE, "User %s joined room %s\n", user->username, room->name);
    broadcast_room(room, msg, user);
}

void broadcast_room(Room* room, const char* message, User* sender) {
    pthread_mutex_lock(&rooms_mutex);
    
    User* current = room->users;
    while (current != NULL) {
        if (current->socket != sender->socket) {
            char formatted_msg[BUFFER_SIZE];
            snprintf(formatted_msg, BUFFER_SIZE, "[%s] %s: %s\n", 
                    room->name, sender->username, message);
            send(current->socket, formatted_msg, strlen(formatted_msg), 0);
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&rooms_mutex);
}

// Utility functions
User* find_user_by_name(const char* username) {
    pthread_mutex_lock(&users_mutex);
    
    User* current = users;
    while (current != NULL) {
        if (strcmp(current->username, username) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&users_mutex);
    return NULL;
}

Room* find_room_by_name(const char* room_name) {
    pthread_mutex_lock(&rooms_mutex);
    
    Room* current = rooms;
    while (current != NULL) {
        if (strcmp(current->name, room_name) == 0) {
            pthread_mutex_unlock(&rooms_mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&rooms_mutex);
    return NULL;
}
