#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <vector> 

#define MAX_COMMODITIES 11
#define Max_Buffer_size 40



// Structure for shared memory
struct SharedBuffer {
    double prices[MAX_COMMODITIES][5] ;       // Current prices
    int write_index[MAX_COMMODITIES];          // Write index for circular buffer to allow continous addition of prices. represent the next avaialable index to be written in.                        
    double buffer_prices[Max_Buffer_size];      // Pointer to array for prices (inside shared memory)
    double buffer_comm_index[Max_Buffer_size];  // Pointer to array for commodity indexes (inside shared memory)
    int front;               // Front of the queue
    int rear;                // Rear of the queue
    int count;               // Number of elements in the buffer
    int buffer_size;         // Buffer size
};
// Initialize the buffer inside shared memory
void initBuffer(SharedBuffer* sb, int buffer_size) {
    sb->front = 0;
    sb->rear = 0;
    sb->count = 0;
    sb->buffer_size = buffer_size;
}

// Push values into the buffer
void push(SharedBuffer* sb, int price, int comm_index) {
    if (sb->count < sb->buffer_size) {
        sb->buffer_prices[sb->rear] = price;
        sb->buffer_comm_index[sb->rear] = comm_index;
        sb->rear = (sb->rear + 1) % sb->buffer_size;  // Wrap around
        sb->count++;
    } else {
        std::cerr << "Buffer is full! Cannot push.\n";
    }
}

// Pop values from the buffer
void pop(SharedBuffer* sb, int& price, int& comm_index) {
    if (sb->count > 0) {
        price = sb->buffer_prices[sb->front];
        comm_index = sb->buffer_comm_index[sb->front];
        sb->front = (sb->front + 1) % sb->buffer_size;  // Wrap around
        sb->count--;
    } else {
        std::cerr << "Buffer is empty! Cannot pop.\n";
    }
}
int shm_id;
SharedBuffer *shared_buffer = nullptr;
int sem_mutex_id = 0;
int sem_filled_id = 0;
int sem_available_id = 0;

void handle_sigint(int sig) {
    if (shared_buffer) {
        // Detach from shared memory
        if (shmdt(shared_buffer) == -1) {
            perror("Failed to detach shared memory");
        } else {
            printf("Shared memory detached successfully.\n");
        }
    }

    // Remove shared memory (only in consumer to avoid producers deleting it)
    if (shmctl(shm_id, IPC_RMID, nullptr) == -1) {
        perror("Failed to delete shared memory");
    } else {
        printf("Shared memory deleted successfully.\n");
    }

    if (sem_mutex_id != 0 || sem_filled_id != 0 || sem_available_id != 0) {
        if (semctl(sem_mutex_id, 0, IPC_RMID) == -1) {
        perror("Removing the mutex semaphore failed");
        exit(EXIT_FAILURE);
    }
    if (semctl(sem_available_id, 0, IPC_RMID) == -1) {
        perror("Removing the available semaphore failed");
        exit(EXIT_FAILURE);
    }
    if (semctl(sem_filled_id, 0, IPC_RMID) == -1) {
        perror("Removing the filled semaphore failed");
        exit(EXIT_FAILURE);
    }

    printf("Semaphores removed successfully.\n");
        
    }

    exit(0); // Terminate program
}

 // Define the commodity names
const char* predefined_commodities[MAX_COMMODITIES] = {
    "ALUMINIUM",
    "COPPER",
    "COTTON",
    "CRUDEOIL",
    "GOLD",
    "LEAD",
    "MENTHAOIL",
    "NATURAL_GAS",
    "NICKEL",
    "SILVER",
    "ZINC"
};

// Union for semaphore control
union semun {
    int val;               // Value for SETVAL
    struct semid_ds *buf;  // Buffer for IPC_STAT, IPC_SET
    unsigned short *array; // Array for GETALL, SETALL
};

// Decrease the semaphore value 
void semWait(int semid) {
    struct sembuf sop = {0, -1, 0};
    if (semop(semid, &sop, 1) == -1) {
        perror("semWait failed");
        exit(EXIT_FAILURE);
    }
}

// Increase the semaphore value 
void semSignal(int semid) {
    struct sembuf sop = {0, 1, 0};
    if (semop(semid, &sop, 1) == -1) {
        perror("semSignal failed");
        exit(EXIT_FAILURE);
    }
}

std :: string prev_price_color[MAX_COMMODITIES] = {""};
std :: string prev_price_arrow[MAX_COMMODITIES] = {" "};
std :: string prev_avg_color[MAX_COMMODITIES] = {""};
std :: string prev_avg_arrow[MAX_COMMODITIES] = {" "};

void display_dashboard(double current_prices[], double current_avg[], double prev_price[], double prev_avg[]) {
    // Clear screen
    printf("\e[1;1H\e[2J");

    std::cout << "Commodity Dashboard\n";
    std::cout << "==================================================\n";
    std::cout << std::setw(16) << "CURRENCY" << std::setw(13) << "PRICE" << std::setw(22) << "AVERAGE PRICE" << "\n";

    for (int i = 0; i < MAX_COMMODITIES; i++) {
         std::cout << std::setw(12) << std::right << predefined_commodities[i] << ": " << std::fixed << std::setprecision(2);
          if (current_prices[i] < prev_price[i]) {
                    std::cout << "\033[1;31m" << std::setw(15) << current_prices[i] << "\u2193" << "\033[0m";
                    prev_price_color[i] = "\033[1;31m";
                    prev_price_arrow[i] = "\u2193";
                } else if (current_prices[i] > prev_price[i]) {
                    std::cout << "\033[1;32m" << std::setw(15) << current_prices[i] << "\u2191" <<"\033[0m";
                    prev_price_color[i] = "\033[1;32m";
                    prev_price_arrow[i] = "\u2191";
                } else {
                    std::cout << prev_price_color[i] << std::setw(15) << current_prices[i] << std::setw(1) << prev_price_arrow[i] <<"\033[0m";
                }

                std::cout << std::fixed << std::setprecision(2);

                if (current_avg[i] < prev_avg[i]) {
                    std::cout << "\033[1;31m" << std::setw(14) << current_avg[i] << "\u2193" << "\033[0m";
                    prev_avg_color[i] = "\033[1;31m";
                    prev_avg_arrow[i] = "\u2193";
                } else if (current_avg[i] > prev_avg[i]) {
                    std::cout << "\033[1;32m" << std::setw(14) << current_avg[i] << "\u2191" << "\033[0m";
                    prev_avg_color[i] = "\033[1;32m";
                    prev_avg_arrow[i] = "\u2191";
                } else {
                     std::cout << prev_avg_color[i] << std::setw(14) << current_avg[i] << prev_avg_arrow[i] <<"\033[0m";
                }
                 
                // std::cout << std::setw(15) << std::fixed << std::setprecision(2);

                std::cout << "\n";
        
    }
}

// ===========================================================================================
// Main function
int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Error not enough arguments sent.\nUsage: ./consumer <BUFFER_SIZE>\n";
        return 1;
    }

    signal(SIGINT, handle_sigint); // listen for termination process and calls handle_siginit dunction.
    int buffer_size = std::stoi(argv[1]);
    if (buffer_size >= Max_Buffer_size){
        perror("Invalid Buffer size, buffer size entered is greater than the Max Buffer size");
        return 1;
    }
   

    // Generate unique key for shared memory 
    key_t sharedm_key = ftok("consumer", 65);
    if (sharedm_key == -1) {
        perror("Failed to generate shared memory key in the consumer.");
        return 1;
    }

    // Create shared memory
    shm_id = shmget(sharedm_key, sizeof(SharedBuffer), 0666 | IPC_CREAT | IPC_EXCL);
    if (shm_id == -1) {
    if (errno == EEXIST) {
        std::cerr << "Shared memory already exists. Ensure no conflicting memory segments are present.\n";
        std::cerr << "Shared memory id: " << shm_id << " \n";
        shmctl(shm_id, IPC_RMID, nullptr);
    }
    perror("Shared memory creation failed");
    return 1;
    }
    else {
    std::cout << "Shared memory id: " << shm_id << " \n";
    }

    shared_buffer = (SharedBuffer *)shmat(shm_id, nullptr, 0);    
    if (shared_buffer == (void *)-1) {
        perror("Shared memory attachment failed in consumer.");
        shmdt(shared_buffer);
        return 1;
    }

   // Initialize the buffer pointers
    initBuffer(shared_buffer, buffer_size);

    // Initialize the write index array with 0
    memset(shared_buffer->write_index, 0, sizeof(shared_buffer->write_index));
    // Initialize the prices array with 0 
    memset(shared_buffer->prices, 0, sizeof(shared_buffer->prices));
    memset(shared_buffer->buffer_comm_index, 0, sizeof(shared_buffer->buffer_comm_index));
    memset(shared_buffer->buffer_prices, 0, sizeof(shared_buffer->buffer_prices));
  

    // Generate unique keys for sempahores
    key_t sem_mutex_key = ftok("consumer", 70);
     if (sem_mutex_key == -1) {
        perror("Failed to generate a semaphore (mutex) key in the consumer.");
        return 1;
    }
    
    key_t sem_filled_key = ftok("consumer", 71);
     if (sem_filled_key == -1) {
        perror("Failed to generate a semaphore (filled) key in the consumer.");
        return 1;
    }
    
    key_t sem_available_key = ftok("consumer", 72);
     if (sem_available_key == -1) {
        perror("Failed to generate a semaphore (available) key in the consumer.");
        return 1;
    }

    // Create the Mutex semaphore
    sem_mutex_id = semget(sem_mutex_key, 1, 0666 | IPC_CREAT | IPC_EXCL); 
    if (sem_mutex_id == -1) {
         if (errno == EEXIST) {
        std::cerr << "Semaphore already exists.\n";
        semctl(sem_mutex_id, 0, IPC_RMID); // Remove the semaphore
         }
        perror("Semaphore creation failed in consumer.");
        return 1;
    }
    else {std::cerr << "Mutex Semaphore id: " << sem_mutex_id << " \n";}
    union semun sem_union;
    sem_union.val = 1; 
    if (semctl(sem_mutex_id, 0, SETVAL, sem_union) == -1) {  // Mutex semaphore initialized to 1
        perror("semctl for mutex failed");
        exit(EXIT_FAILURE);
    }

    // Create the Availabe semaphore
    sem_available_id = semget(sem_available_key, 1, 0666 | IPC_CREAT | IPC_EXCL); 
    if (sem_available_id== -1) {
         if (errno == EEXIST) {
        std::cerr << "Semaphore already exists.\n";
        semctl(sem_available_id, 0, IPC_RMID); // Remove the semaphore
         }
        perror("Semaphore creation failed in consumer.");
        return 1;
    }
    else {std::cerr << "Availble Semaphore id: " << sem_available_id << " \n";}

    sem_union.val = buffer_size; 
    if (semctl(sem_available_id, 0, SETVAL, sem_union) == -1) {  // Available semaphore initialized to buffersize
        perror("semctl for available failed");
        exit(EXIT_FAILURE);
    }

    // Create the filled semaphore
    sem_filled_id = semget(sem_filled_key, 1, 0666 | IPC_CREAT | IPC_EXCL); 
    if (sem_filled_id== -1) {
         if (errno == EEXIST) {
        std::cerr << "Semaphore already exists.\n";
        semctl(sem_filled_id, 0, IPC_RMID); // Remove the semaphore
         }
        perror("Semaphore creation failed in consumer.");
        return 1;
    }
    else {std::cerr << "Filled Semaphore id: " << sem_filled_id << " \n";}

    sem_union.val = 0; 
    if (semctl(sem_filled_id, 0, SETVAL, sem_union) == -1) {  // Filled semaphore initialized to 0
        perror("semctl for available failed");
        exit(EXIT_FAILURE);
    }
    
        
// Initialize the current prices and average prices
    double current_prices[MAX_COMMODITIES] = {0.00};
    double current_avg[MAX_COMMODITIES] = {0.00};
    double prev_price[MAX_COMMODITIES] = {0.00};
    double prev_avg[MAX_COMMODITIES] = {0.00};

    while (true) {
        display_dashboard(current_prices, current_avg, prev_price, prev_avg);

        semWait(sem_filled_id); // Wait until at least one producer produces.
        semWait(sem_mutex_id); // Lock mutex
        
    // -------------------------------------Critical Section-------------------------------------

       
        
        int bufferprice, comm_index;
        pop(shared_buffer, bufferprice, comm_index); // Pop the price and commodity index from the buffer
        int index = shared_buffer->write_index[comm_index]; // Get the current write index for commodity i
        double latest_price;
        double average_val = 0.00;
        if (latest_price != bufferprice){
            perror("something is wrong with the buffer");
        }
        
         // Check for circular buffer edge case
         if (index == 0) {
             latest_price = shared_buffer->prices[comm_index][4]; // Access the last position if index is 0
         } else {
             latest_price = shared_buffer->prices[comm_index][index - 1]; // Access the most recent price otherwise
         }

         if (shared_buffer->prices[comm_index][index] > 0) {
            average_val = (shared_buffer->prices[comm_index][0] + shared_buffer->prices[comm_index][1] + shared_buffer->prices[comm_index][2] + shared_buffer->prices[comm_index][3] + shared_buffer->prices[comm_index][4] ) / 5;
            }

        // Store the last price and average price of each commodity
        prev_price[comm_index] = current_prices[comm_index];
        current_prices[comm_index] = latest_price;
        prev_avg[comm_index] = current_avg[comm_index];
        current_avg[comm_index] = average_val;
        

    // -------------------------------------End of Critical Section-------------------------------------

        semSignal(sem_mutex_id); // Unlock mutex
        semSignal(sem_available_id); // Signal filled

    }

    return 0;
}
