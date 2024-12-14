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
    char commodities[MAX_COMMODITIES][20];   // Commodity names
    double prices[MAX_COMMODITIES][5] ;       // Current prices
    int write_index[MAX_COMMODITIES];          // Write index for circular buffer to allow continous addition of prices. represent the next avaialable index to be written in.
    int prices_count;          // To count the number of prices added to the buffer to be compared with buffer size.                            
};

// // Semaphore operations
// void sem_wait(int sem_id, int sem_num) {
//     struct sembuf sb = {sem_num, -1, 0};
//     semop(sem_id, &sb, 1);
// }

// void sem_signal(int sem_id, int sem_num) {
//     struct sembuf sb = {sem_num, 1, 0};
//     semop(sem_id, &sb, 1);
// }

int shm_id;
SharedBuffer *shared_buffer = nullptr;

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


// ===========================================================================================


int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Error not enough arguments sent.\nUsage: ./consumer <BUFFER_SIZE>\n";
        return 1;
    }

    signal(SIGINT, handle_sigint); // listen for termination process and calls handle_siginit dunction.
    int buffer_size = std::stoi(argv[1]);

    // Generate unique key for shared memory and semaphores
    key_t sharedm_key = ftok("consumer", 65);
    if (sharedm_key == -1) {
        perror("Failed to generate shared memory key in the consumer.");
        return 1;
    }

    // key_t sem_key = ftok("consumer", 75);
    
    // Create shared memory
    // size_t aligned_size = ((sizeof(SharedBuffer) + getpagesize() - 1) / getpagesize()) * getpagesize();

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
    // shmdt(shared_buffer);
    if (shared_buffer == (void *)-1) {
        perror("Shared memory attachment failed in consumer.");
        // shmdt(shared_buffer);
        return 1;
    }


    // Initialize the write index array with 0
    memset(shared_buffer->write_index, 0, sizeof(shared_buffer->write_index));
    shared_buffer->prices_count = 0;
    // Initialize the prices array with 0 
    memset(shared_buffer->prices, 0, sizeof(shared_buffer->prices));
  

// filling the commodities array 
for (int i = 0; i < MAX_COMMODITIES; i++) {
    strncpy(shared_buffer->commodities[i], predefined_commodities[i], 19);
    int len = strlen(predefined_commodities[i]);
    shared_buffer->commodities[i][len] = '\0'; // Ensure null-termination
}

    // // Create semaphores
    // int sem_id = semget(sem_key, 3, 0666 | IPC_CREAT);
    // if (sem_id == -1) {
    //     perror("Semaphore creation failed");
    //     return 1;
    // }

    // semctl(sem_id, 0, SETVAL, buffer_size); // Empty slots
    // semctl(sem_id, 1, SETVAL, 1);          // Mutex
    // semctl(sem_id, 2, SETVAL, 0);          // Full slots
    
        double prev_price[MAX_COMMODITIES] = {0.0};
        double prev_avg[MAX_COMMODITIES] = {0.0};
        double latest_price = 0.0;
        double average_val = 0.0;

        while (true) {
            // Clear screen
            printf("\e[1;1H\e[2J");

            std::cout << "Commodity Dashboard\n";
            std::cout << "===================\n";
            std::cout << std::setw(15) << "CURRENCY" << std::setw(13) << "PRICE" << std::setw(22) << "AVERAGE PRICE" << "\n";

            for (int i = 0; i < MAX_COMMODITIES; ++i) {
                int index = shared_buffer->write_index[i];

                if (index == 0) {
                    latest_price = shared_buffer->prices[i][4];
                } else {
                    latest_price = shared_buffer->prices[i][index - 1];
                }

                average_val = 0.0;
                if (shared_buffer->prices[i][index] > 0) {
                    average_val = (shared_buffer->prices[i][0] + shared_buffer->prices[i][1] + shared_buffer->prices[i][2] + shared_buffer->prices[i][3] + shared_buffer->prices[i][4]) / 5;
                }

                std::cout << std::setw(12) << shared_buffer->commodities[i] << ": " << std::setw(12) << std::fixed << std::setprecision(2);

                if (latest_price < prev_price[i]) {
                    std::cout<< std::setw(15) << "\033[1;31m" << latest_price << "\u2193" << "\033[0m";;
                } else if (latest_price > prev_price[i]) {
                    std::cout << std::setw(15) << "\033[1;32m" << latest_price << "\u2191" <<"\033[0m";;
                } else {
                    std::cout << latest_price;
                }

                std::cout << std::setw(15) << std::fixed << std::setprecision(2);

                if (average_val < prev_avg[i]) {
                    std::cout << std::setw(15) << "\033[1;31m" << " " << average_val << "\u2193" << "\033[0m";
                } else if (average_val > prev_avg[i]) {
                    std::cout << std::setw(15) << "\033[1;32m" << " " << average_val << "\u2191" << "\033[0m";
                } else {
                    std::cout << average_val;
                }

                std::cout << "\n";

                prev_price[i] = latest_price;
                prev_avg[i] = average_val;
            }

            usleep(2000 * 1000);
        }
    return 0;
}
