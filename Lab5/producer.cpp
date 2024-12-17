#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <random>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <cstdlib>
#include <csignal>
#include <time.h>
#include <cstring>
#include <bits/algorithmfwd.h>
#include <algorithm>
#include <cctype>
// #include <ctime>
#include <queue>

#define MAX_COMMODITIES 11
#define Max_Buffer_size 40

// Structure for shared memory
struct SharedBuffer {
    double prices[MAX_COMMODITIES][5];        // Current prices saved to calc average
    int write_index[MAX_COMMODITIES];         // Write index for circular buffer to allow continous addition of prices.           
    double buffer_prices[Max_Buffer_size];      // Pointer to array for prices (inside shared memory)
    double buffer_comm_index[Max_Buffer_size];  // Pointer to array for commodity indexes (inside shared memory)
    int front;               // Front of the queue
    int rear;                // Rear of the queue
    int count;               // Number of elements in the buffer
    int buffer_size;         // Buffer size
};

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


const char* predefined_commodities[MAX_COMMODITIES] = {
    "ALUMINIUM",
    "COPPER",
    "COTTON",
    "CRUDEOIL",
    "GOLD",
    "LEAD",
    "MENTHAOIL",
    "NATURAL GAS",
    "NICKEL",
    "SILVER",
    "ZINC"
};

int get_commodity_index(std::string comm){
    // Convert string to uppercase
      std::transform(comm.begin(), comm.end(), comm.begin(),
                   [](unsigned char c) { return std::toupper(c); });


        for (int i = 0 ; i < MAX_COMMODITIES; i++){
            if (predefined_commodities[i] == comm){
                return i;
            }
        }
        return -1;
    };

//  Get formatted current time
std::string get_time() {
    timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("error in clock_gettime");
        exit(EXIT_FAILURE);
    }

    char time_str[30]; // Format: [MM/DD/YYYY HH:MM:SS.SSS]
    strftime(time_str, sizeof(time_str), "[%m/%d/%Y %H:%M:%S.", localtime(&ts.tv_sec));
    snprintf(time_str + strlen(time_str), sizeof(time_str) - strlen(time_str), ".%03ld", ts.tv_nsec / 1000000); // to convert from nano second to ms divide by 10^6
    return std::string(time_str);
}

int shm_id;
SharedBuffer *shared_buffer = nullptr;


// Semaphore operations
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

void handle_sigint(int sig) {
    if (shared_buffer) {
        // Detach from shared memory
        if (shmdt(shared_buffer) == -1) {
            perror("Failed to detach shared memory");
        } else {
            printf("Shared memory detached successfully.\n");
        }
    }

    exit(0); // Terminate program
}


int main(int argc, char *argv[]) {
    if (argc != 6) {
        std::cerr << "Error not enough arguments passed.\nUsage: ./producer <COMMODITY_NAME> <MEAN> <STD_DEV> <SLEEP_MS> <BUFFER_SIZE>\n";
        return 1;
    }
    signal(SIGINT, handle_sigint);

    // Parse command-line arguments
    std::string commodity_name = argv[1];
    int comm_index = get_commodity_index(commodity_name); 
    if (comm_index == -1){
        std::cerr << "Error Invalid Commodity name.\nMust enter one of these:\n\nALUMINIUM\nCOPPER\nCOTTON\nCRUDEOIL\nGOLD\nLEAD\nMENTHAOIL\nNATURAL_GAS\nNICKEL\nSILVER\nZINC\n";
        return 1;
    }
    
    double mean = std::stod(argv[2]);
    double std_dev = std::stod(argv[3]);
    int sleep_interval = std::stoi(argv[4]);
    int buffer_size = std::stoi(argv[5]);

   
    // Generate unique key for shared memory and semaphores
 
    key_t sharedm_key = ftok("consumer", 65); // uses same consumer key to access the shared memory
    if (sharedm_key == -1) {
        perror("Failed to generate shared memory key in the Producer");
        return 1;
    }
    

    // Check if shared memory exists
    shm_id = shmget(sharedm_key, sizeof(SharedBuffer) , 0666); // Doesnt create the shared memory if doesnt exist
    if (shm_id == -1) {
        if (errno == ENOENT) {
            std::cerr << "Error: Shared memory does not exist. Please run the consumer first to create shared memory.\n";
        } else {
            perror("Failed to access shared memory for the producer.");
        }
        return 1;
    }

      // Attach to the existing shared memory
    shared_buffer = (SharedBuffer *)shmat(shm_id, nullptr, 0);
    if (shared_buffer == (void *)-1) {
        perror("Failed to attach to shared memory from the producer.");
        return 1;
    }
     if (buffer_size != shared_buffer->buffer_size){
        perror("Invalid Buffer size, buffer size entered is not the same as consumer");
        return 1;
    }

    // Getting semaphore keys from the consumer
    key_t sem_mutex_key = ftok("consumer", 70);
    key_t sem_filled_key = ftok("consumer", 71);
    key_t sem_available_key = ftok("consumer", 72);
    if(sem_mutex_key == -1 || sem_filled_key == -1 || sem_available_key == -1){
        perror("Failed to generate semaphore keys in the producer.");
        return 1;
    }
    
    // Getting the created mutex semaphore
    int sem_mutex_id =  semget(sem_mutex_key, 1, 0666);
    if (sem_mutex_id == -1) {
        if (errno == ENOENT) {
            std::cerr << "Error: (Mutex) semaphore does not exist. Please run the consumer first to create semaphores.\n";
        } else {
            perror("Failed to get (mutex) semaphore for the producer.");
        }
        return 1;
    }
    
    // Getting the created filled semaphore
    int sem_filled_id = semget(sem_filled_key, 1, 0666);
    if (sem_filled_id == -1) {
        if (errno == ENOENT) {
            std::cerr << "Error: (Filled) semaphore does not exist. Please run the consumer first to create semaphores.\n";
        } else {
            perror("Failed to get (filled) semaphore for the producer.");
        }
        return 1;
    }
    
    // Getting the created available semaphore
    int sem_available_id = semget(sem_available_key, 1, 0666);
    if (sem_available_id == -1) {
        if (errno == ENOENT) {
            std::cerr << "Error: (Available) semaphore does not exist. Please run the consumer first to create semaphores.\n";
        } else {
            perror("Failed to get (available) semaphore for the producer.");
        }
        return 1;
    }
    
    printf("Producer connected to semaphores successfully.\n");


    std::default_random_engine generator;
    std::normal_distribution<double> distribution(mean, std_dev);

    while (true) {
        // Generate new price
        double price = distribution(generator);

        // Log generating a new value
        std::cerr << get_time() << "] " << commodity_name << ": generating a new value " << price << "\n";

        // Wait on empty and mutex
        std::cerr << get_time() << "] " << commodity_name << ": trying to get mutex on shared buffer\n";
        semWait(sem_available_id); // Wait for the buffer to be available
        semWait(sem_mutex_id); // Lock mutex

        // -------------------------------------Critical Section------------------------------------------------------------------

        printf("Producer have waited on mutex and entered critical section.\n");

        // Write to shared memory
        int index = shared_buffer->write_index[comm_index];
        shared_buffer->prices[comm_index][index] = price;
        shared_buffer->write_index[comm_index] = (index + 1) % 5;
        push(shared_buffer, price, comm_index);
        
        // Log placing value
        std::cerr << get_time() << "] " << commodity_name << ": placing " << price << " on shared buffer\n";
        
        //-------------------------------------End of Critical Section--------------------------------------------------------------

        semSignal(sem_mutex_id); // Unlock mutex
        semSignal(sem_filled_id); // Signal filled
        printf("Producer have exited the critical section.\n");

        std::cerr << get_time() << "] " << commodity_name << ": sleeping for " << sleep_interval << " ms\n\n\n";
         // Sleep
        usleep(sleep_interval * 1000); // Convert to microseconds
    }

    return 0;
}
