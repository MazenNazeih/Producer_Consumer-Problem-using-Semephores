#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <cstring>
#include <cstdio>

#define MAX_COMMODITIES 40

// Structure for shared memory
struct SharedBuffer {
    char commodities[MAX_COMMODITIES][10]; // Commodity names
    double prices[MAX_COMMODITIES];        // Current prices
    int write_index;                       // Write index for circular buffer
};

// Semaphore operations
void sem_wait(int sem_id, int sem_num) {
    struct sembuf sb = {sem_num, -1, 0};
    semop(sem_id, &sb, 1);
}

void sem_signal(int sem_id, int sem_num) {
    struct sembuf sb = {sem_num, 1, 0};
    semop(sem_id, &sb, 1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./consumer <BUFFER_SIZE>\n";
        return 1;
    }

    int buffer_size = std::stoi(argv[1]);

    // Generate unique key for shared memory and semaphores
    key_t shm_key = ftok("consumer", 65);
    key_t sem_key = ftok("consumer", 75);

    // Create shared memory
    int shm_id = shmget(shm_key, sizeof(SharedBuffer), 0666 | IPC_CREAT);
    if (shm_id == -1) {
        perror("Shared memory creation failed");
        return 1;
    }
    SharedBuffer *shared_buffer = (SharedBuffer *)shmat(shm_id, nullptr, 0);
    if (shared_buffer == (void *)-1) {
        perror("Shared memory attachment failed");
        return 1;
    }

    // Initialize shared buffer
    shared_buffer->write_index = 0;
    memset(shared_buffer->commodities, 0, sizeof(shared_buffer->commodities));
    memset(shared_buffer->prices, 0, sizeof(shared_buffer->prices));

    // Create semaphores
    int sem_id = semget(sem_key, 3, 0666 | IPC_CREAT);
    if (sem_id == -1) {
        perror("Semaphore creation failed");
        return 1;
    }

    semctl(sem_id, 0, SETVAL, buffer_size); // Empty slots
    semctl(sem_id, 1, SETVAL, 1);          // Mutex
    semctl(sem_id, 2, SETVAL, 0);          // Full slots

    while (true) {
        // Clear screen
        printf("\e[1;1H\e[2J");

        std::cout << "Commodity Dashboard\n";
        std::cout << "===================\n";

        for (int i = 0; i < buffer_size; ++i) {
            sem_wait(sem_id, 2); // Wait for full
            sem_wait(sem_id, 1); // Lock mutex

            if (strlen(shared_buffer->commodities[i]) > 0) {
                std::cout << std::setw(10) << shared_buffer->commodities[i]
                          << " : " << std::fixed << std::setprecision(2)
                          << shared_buffer->prices[i] << "\n";
            }

            sem_signal(sem_id, 1); // Unlock mutex
            sem_signal(sem_id, 0); // Signal empty
        }

        sleep(1);
    }

    // Detach and destroy shared memory
    shmdt(shared_buffer);
    shmctl(shm_id, IPC_RMID, nullptr);

    return 0;
}
