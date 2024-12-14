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

#define MAX_COMMODITIES 11

// Structure for shared memory
struct SharedBuffer {
    double prices[MAX_COMMODITIES][5] ;       // Current prices
    int write_index[MAX_COMMODITIES];          // Write index for circular buffer to allow continous addition of prices. represent the next avaialable index to be written in.                        
};

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
    "NATURAL GAS",
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
std :: string prev_price_arrow[MAX_COMMODITIES] = {""};
std :: string prev_avg_color[MAX_COMMODITIES] = {""};
std :: string prev_avg_arrow[MAX_COMMODITIES] = {""};

void display_dashboard(double current_prices[], double current_avg[], double prev_price[], double prev_avg[]) {
    // Clear screen
    printf("\e[1;1H\e[2J");

    std::cout << "Commodity Dashboard\n";
    std::cout << "==================================================\n";
    std::cout << std::setw(15) << "CURRENCY" << std::setw(13) << "PRICE" << std::setw(22) << "AVERAGE PRICE" << "\n";

    for (int i = 0; i < MAX_COMMODITIES; i++) {
         std::cout << std::setw(12) << predefined_commodities[i] << ": " << std::setw(12) << std::fixed << std::setprecision(2);
          if (current_prices[i] < prev_price[i]) {
                    std::cout<< std::setw(15) << "\033[1;31m" << current_prices[i] << "\u2193" << "\033[0m";;
                    prev_price_color[i] = "\033[1;31m";
                    prev_price_arrow[i] = "\u2193";
                } else if (current_prices[i] > prev_price[i]) {
                    std::cout << std::setw(15)  << "\033[1;32m" << current_prices[i] << "\u2191" <<"\033[0m";;
                    prev_price_color[i] = "\033[1;32m";
                    prev_price_arrow[i] = "\u2191";
                } else {
                    std::cout << std::setw(15)  << prev_price_color[i] << current_prices[i] << prev_price_arrow[i] <<"\033[0m";
                }

                std::cout << std::setw(15) << std::fixed << std::setprecision(2);

                if (current_avg[i] < prev_avg[i]) {
                    std::cout << std::setw(15)  << "\033[1;31m" << current_avg[i] << "\u2193" << "\033[0m";
                    prev_avg_color[i] = "\033[1;31m";
                    prev_avg_arrow[i] = "\u2193";
                } else if (current_avg[i] > prev_avg[i]) {
                    std::cout << std::setw(15) << "\033[1;32m" << current_avg[i] << "\u2191" << "\033[0m";
                    prev_avg_color[i] = "\033[1;32m";
                    prev_avg_arrow[i] = "\u2191";
                } else {
                     std::cout << std::setw(15) << prev_avg_color[i] << current_avg[i] << prev_avg_arrow[i] <<"\033[0m";
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


    // Initialize the write index array with 0
    memset(shared_buffer->write_index, 0, sizeof(shared_buffer->write_index));
    // Initialize the prices array with 0 
    memset(shared_buffer->prices, 0, sizeof(shared_buffer->prices));
  

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

        for (int i = 0; i < MAX_COMMODITIES ; ++i) { // must loop on buffer size to change only the commodities being modified-> not effecient as i will have to save a dirty bit.
        
        int index = shared_buffer->write_index[i]; // Get the current write index for commodity i
        double latest_price;
        double average_val = 0.00;
        
         // Check for circular buffer edge case
         if (index == 0) {
             latest_price = shared_buffer->prices[i][4]; // Access the last position if index is 0
         } else {
             latest_price = shared_buffer->prices[i][index - 1]; // Access the most recent price otherwise
         }

         if (shared_buffer->prices[i][index] > 0) {
            average_val = (shared_buffer->prices[i][0] + shared_buffer->prices[i][1] + shared_buffer->prices[i][2] + shared_buffer->prices[i][3] + shared_buffer->prices[i][4] ) / 5;
            }

        // Store the last price and average price of each commodity
        prev_price[i] = current_prices[i];
        current_prices[i] = latest_price;
        prev_avg[i] = current_avg[i];
        current_avg[i] = average_val;
        
        }

    // -------------------------------------End of Critical Section-------------------------------------

        semSignal(sem_mutex_id); // Unlock mutex
        semSignal(sem_available_id); // Signal filled

    }

    return 0;
}
