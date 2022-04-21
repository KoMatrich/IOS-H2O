// proj2.c
//  Řešení IOS - projekt 2, 11.4.2022
//	 Autor: Martin Kocich, FIT
//	 Přeloženo : gcc 10.2.0

#include <grp.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *outputFileName = "proj2.out";

static FILE *file; // file output
int shmid;         // shared memory id

static uint no, nh, ti, tb; // input arguments
static const int oxy_c = 1, hyd_c = 2;

// check if string is unsigned int
bool isUInt(char *in)
{
    while (*in != '\0')
    {
        if (*in < '0' || *in > '9')
            return false;
        in++;
    }
    return true;
}

// shared data struct
typedef struct
{
    uint actionCount;      // index of file write
    pthread_mutex_t fileM; // file write mutex

    bool done;             // flag for end of production
    uint molecules;        // number of created molecules
    sem_t facWait;         // semaphore for factory

    uint hydWC;            // count of waiting hydrogen atoms
    pthread_mutex_t hydCM; // hydrogen count mutex
    sem_t hydWait;         // semaphore for waiting hydrogen atoms

    uint oxyWC;            // count of waiting oxygen atoms
    pthread_mutex_t oxyCM; // oxygen count mutex
    sem_t oxyWait;         // semaphore for waiting oxygen atoms
} shmDataT;

void init(shmDataT *data)
{
    sem_init(&data->facWait, 0, 0);
    sem_init(&data->hydWait, 0, 0);
    sem_init(&data->oxyWait, 0, 0);
    data->actionCount = 0;
    data->done = false;
    data->hydWC = 0;
    data->oxyWC = 0;
}

// shared data destructor
void des(shmDataT *data)
{
    pthread_mutex_destroy(&data->fileM);
    pthread_mutex_destroy(&data->hydCM);
    pthread_mutex_destroy(&data->oxyCM);
    sem_destroy(&data->facWait);
    sem_destroy(&data->oxyWait);
    sem_destroy(&data->hydWait);
    shmdt(data);
}

// prints to file with index
static inline void printToFile(shmDataT *data, const char *fmt, ...)
{
    // locks writing to file or wait until is free
    pthread_mutex_lock(&data->fileM);

    // get link to shared memory
    shmDataT *shmp = shmat(shmid, NULL, 0);
    fprintf(file, "%d: ", shmp->actionCount);
    shmp->actionCount++;
    // unlink pointer to shared memory
    if (shmdt(shmp) == -1)
    {
        perror("shmdt");
        exit(1);;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    // flush everything to file
    fflush(file);

    pthread_mutex_unlock(&data->fileM);
}

// process args and saves them to global variables
void processArgs(int argc, char **argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "Wrong number of arguments. (%d of 4)\n", argc - 1);
        exit(1);
    }

    // checks if all arguments are unsigned integers
    for (uint i = 1; i <= 4; ++i)
    {
        bool allInt = isUInt(argv[i]);
        if (!allInt)
        {
            fprintf(stderr,
                    "%d. entered argument '%s' have to be positive integers.\n", i,
                    argv[i]);
            exit(1);
        }
    }

    no = atoi(argv[1]);
    nh = atoi(argv[2]);
    ti = atoi(argv[3]);
    tb = atoi(argv[4]);

    // zero check
    if (!((no > 0) && (nh > 0) && (ti > 0) && (tb > 0)))
    {
        fprintf(stderr, "Entered numbers have to be nonzero numbers.\n");
        exit(1);
    }
}

void runInit(){
    // shared memory initialization
    shmid = shmget(IPC_PRIVATE, sizeof(shmDataT), IPC_CREAT | 0660);
    if (shmid == -1)
    {
        perror("Shared memory");
        exit(1);
    }
    shmDataT *shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("Shared memory attach");
        exit(1);
    }

    init(shmp);

    // initialize shared memory
    file = fopen(outputFileName, "w");
    if (file == NULL)
    {
        fprintf(stderr, "Cant create new file for output.\n");
        shmdt(shmp);
        exit(1);
    }

    // initialize mutexes
    if (pthread_mutex_init(&shmp->fileM, NULL) != 0)
    {
        fclose(file);
        printf("mutex init failed\n");
        shmdt(shmp);
        exit(1);
    }

    if (pthread_mutex_init(&shmp->hydCM, NULL) != 0)
    {
        fclose(file);
        pthread_mutex_destroy(&shmp->fileM);
        printf("mutex init failed\n");
        shmdt(shmp);
        exit(1);
    }

    if (pthread_mutex_init(&shmp->oxyCM, NULL) != 0)
    {
        fclose(file);
        pthread_mutex_destroy(&shmp->fileM);
        pthread_mutex_destroy(&shmp->hydCM);
        printf("mutex init failed\n");
        shmdt(shmp);
        exit(1);
    }

    if (shmdt(shmp) == -1)
    {
        fclose(file);
        pthread_mutex_destroy(&shmp->fileM);
        pthread_mutex_destroy(&shmp->hydCM);
        pthread_mutex_destroy(&shmp->oxyCM);
        perror("shmdt\n");
        shmdt(shmp);
        exit(1);
    }



    // sets random generator of main process
    srand(time(0));
}

void oxygen(int seed, pid_t id, shmDataT *data){
    printToFile(data, "A: O %d: started\n", id);

    srand(seed);
    usleep(rand() % tb);
    printToFile(data, "A: O %d: going to queue\n", id);

    return;

    pthread_mutex_lock(&data->oxyCM);
    int oxy = data->oxyWC++;
    int hyd = data->hydWC;

    if (hyd >= hyd_c && oxy >= oxy_c)
    {
        sem_post(&data->facWait);
    }
    pthread_mutex_unlock(&data->oxyCM);

    if (!data->done){
        sem_wait(&data->oxyWait);
        int molecule = data->molecules;
        printToFile(data, "A: O %d: creating molecule %d\n", id, molecule);
        usleep(rand() % tb);
        printToFile(data, "A: O %d: finished creating molecule %d\n", id, molecule);
    }else{
        printToFile(data, "A: O %d: not enought H\n", id);
    }
}

void hydrogen(int seed, pid_t id, shmDataT *data){
    printToFile(data, "A: H %d: started\n", id);

    srand(seed);
    usleep(rand() % ti);
    printToFile(data, "A: H %d: going to queue\n", id);

    return;

    pthread_mutex_lock(&data->hydCM);
    int hyd = data->hydWC++;
    int oxy = data->oxyWC;

    if (hyd >= hyd_c && oxy >= oxy_c)
    {
        sem_post(&data->facWait);
    }
    pthread_mutex_unlock(&data->hydCM);

    if (!data->done){
        sem_wait(&data->hydWait);
        int molecule = data->molecules;
        printToFile(data, "A: H %d: creating molecule %d\n", id, molecule);
        usleep(rand() % tb);
        printToFile(data, "A: H %d: finished creating molecule %d\n", id, molecule);
    }else{
        printToFile(data, "A: H %d: not enought O\n", id);
    }
}

void factory(shmDataT *data){
    while(!data->done){
        sem_wait(&data->facWait);

        pthread_mutex_lock(&data->hydCM);
        pthread_mutex_lock(&data->oxyCM);
        data->molecules++;

        data->hydWC -= hyd_c;
        for(int i = 0; i < hyd_c; i++)
            sem_post(&data->hydWait);

        data->oxyWC -= oxy_c;
        for(int i = 0; i < oxy_c; i++)
            sem_post(&data->oxyWait);

        if (data->molecules * hyd_c >= nh || data->molecules * oxy_c >= no)
            data->done = true;
        pthread_mutex_unlock(&data->oxyCM);
        pthread_mutex_unlock(&data->hydCM);
    }
}

void factoryInit(){
    // creates threads for hydrogen and oxygen

    for (uint i = 1; i <= no; ++i)
    {
        // seed need to be generated by main process to ensure randomness
        int seed = rand();
        pid_t id = fork();
        if (id == 0)
        {
            shmDataT *shmp = shmat(shmid, NULL, 0);
            oxygen(seed, i, shmp);
            // unlink pointer to shared memory
            if (shmdt(shmp) == -1)
            {
                perror("shmdt");
            }
            exit(0);
        }
    }

    for (uint i = 1; i <= nh; ++i)
    {
        int seed = rand();
        pid_t id = fork();
        if (id == 0)
        {
            shmDataT *shmp = shmat(shmid, NULL, 0);
            hydrogen(seed, i, shmp);
            if (shmdt(shmp) == -1)
            {
                perror("shmdt");
            }
            exit(0);
        }
    }
}

int main(int argc, char **argv)
{
    // argument processing part
    processArgs(argc, argv);

    runInit();
    factoryInit();

    // factory process
    shmDataT *shmp = shmat(shmid, NULL, 0);
    factory(shmp);

    // wait for all to finish
    while (wait(NULL) > 0){}

    printToFile(shmp,"ALL done\n");
    des(shmp);
    fclose(file);
    exit(0);
}