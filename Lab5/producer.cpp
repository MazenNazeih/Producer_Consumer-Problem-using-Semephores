#include <iostream>
#include <fstream>
#include <string>
#include <random>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <ctime>
#include <cstring>

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

// Get formatted current time
std::string get_time() {
    char buffer[30];
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    strftime(buffer, sizeof(buffer), "%m/%d/%Y %H:%M:%S", localtime(&ts.tv_sec));
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), ".%03ld", ts.tv_nsec / 1000000);
    return std::string(buffer);
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: ./producer <COMMODITY_NAME> <MEAN> <STD_DEV> <SLEEP_MS> <BUFFER_SIZE>\n";
        return 1;
    }

    // Parse command-line arguments
    std::string commodity_name = argv[1];
    double mean = std::stod(argv[2]);
    double std_dev = std::stod(argv[3]);
    int sleep_interval = std::stoi(argv[4]);
    int buffer_size = std::stoi(argv[5]);

    // Generate unique key for shared memory and semaphores
    key_t shm_key = ftok("producer_consumer", 65);
    key_t sem_key = ftok("producer_consumer", 75);

    // Attach to shared memory
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

    // Initialize semaphores
    int sem_id = semget(sem_key, 3, 0666 | IPC_CREAT);
    if (sem_id == -1) {
        perror("Semaphore creation failed");
        return 1;
    }

    std::default_random_engine generator;
    std::normal_distribution<double> distribution(mean, std_dev);

    while (true) {
        // Generate new price
        double price = distribution(generator);

        // Log generating a new value
        std::cerr << "[" << get_time() << "] " << commodity_name << ": generating a new value " << price << "\n";

        // Wait on empty and mutex
        std::cerr << "[" << get_time() << "] " << commodity_name << ": trying to get mutex on shared buffer\n";
        sem_wait(sem_id, 0); // Wait on empty
        sem_wait(sem_id, 1); // Lock mutex

        // Write to shared memory
        int index = shared_buffer->write_index;
        strncpy(shared_buffer->commodities[index], commodity_name.c_str(), 10);
        shared_buffer->prices[index] = price;
        shared_buffer->write_index = (index + 1) % buffer_size;

        // Log placing value
        std::cerr << "[" << get_time() << "] " << commodity_name << ": placing " << price << " on shared buffer\n";

        // Signal mutex and full
        sem_signal(sem_id, 1); // Unlock mutex
        sem_signal(sem_id, 2); // Signal full

        // Sleep
        std::cerr << "[" << get_time() << "] " << commodity_name << ": sleeping for " << sleep_interval << " ms\n";
        usleep(sleep_interval * 1000); // Convert to microseconds
    }

    // Detach shared memory
    shmdt(shared_buffer);

    return 0;
}
