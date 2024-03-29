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
static const uint OXY_C = 1, HYC_C = 2;

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
    pthread_mutexattr_t att;

    uint actionCount;      // index of file write
    pthread_mutex_t fileM; // file write mutex

    bool done;      // flag for end of production
    uint molecules; // number of created molecules
    sem_t facWait;  // semaphore for factory

    pthread_mutex_t countM; // atom counter mutex
    uint hydWC;             // count of waiting hydrogen atoms
    uint oxyWC;             // count of waiting oxygen atoms

    sem_t hydWait;      // semaphore for waiting hydrogen atoms
    sem_t oxyWait;      // semaphore for waiting oxygen atoms

    sem_t creationWait; // semaphore for waiting creation of molecule
} shmDataT;

void init(shmDataT *data)
{
    sem_init(&data->facWait, 1, 0);
    sem_init(&data->hydWait, 1, 0);
    sem_init(&data->oxyWait, 1, 0);
    sem_init(&data->creationWait, 1, 0);
    data->actionCount = 1;
    data->done = false;
    data->hydWC = 0;
    data->oxyWC = 0;
}

// shared data destructor
void des(shmDataT *data)
{
    pthread_mutex_destroy(&data->fileM);
    pthread_mutex_destroy(&data->countM);

    sem_destroy(&data->facWait);
    sem_destroy(&data->oxyWait);
    sem_destroy(&data->hydWait);
    sem_destroy(&data->creationWait);
    // deatach shared memory
    shmdt(data);
    // clear shared memory
    shmctl(shmid, IPC_RMID, NULL);
}

// prints to file with index
static inline void printToFile(shmDataT *data, const char *fmt, ...)
{
    // locks writing to file or wait until is free
    pthread_mutex_lock(&data->fileM);

    // prints action index
    fprintf(file, "%d: ", data->actionCount);
    data->actionCount++;

    // prints message
    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    // flush everything to file
    fflush(file);
    // leave empty buffer

    pthread_mutex_unlock(&data->fileM);
}

// process args and saves them to global variables
void processArgs(int argc, char **argv)
{
    if (argc == 1)
    {
        fprintf(stderr, "Usage: %s <no> <nh> <ti> <tb>\n", argv[0]);
        fprintf(stderr, "no - number of oxygen atoms\n");
        fprintf(stderr, "nh - number of hydrogen atoms\n");
        fprintf(stderr, "ti - time interval for atom to join molecule creation que\n");
        fprintf(stderr, "tb - time interval for molecule creation\n");
        exit(1);
    }
    else if (argc != 5)
    {
        fprintf(stderr, "Wrong number of arguments. (%d of 4)\n", argc - 1);
        exit(1);
    }

    // checks if all arguments are unsigned integers
    for (uint i = 1; i <= 4; ++i)
    {
        if (!isUInt(argv[i]))
        {
            fprintf(stderr,
                    "%d. entered argument '%s' have to be positive integer.\n", i,
                    argv[i]);
            exit(1);
        }
    }

    no = atoi(argv[1]);
    nh = atoi(argv[2]);
    ti = atoi(argv[3]);
    tb = atoi(argv[4]);

    // limit check
    if ((ti > 1000) || (tb > 1000))
    {
        fprintf(stderr, "Entered numbers for time have to be less than 1000.\n");
        exit(1);
    }
}

void runInit()
{
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

    // important mutex atributes !
    // mutexes wont work without them !
    pthread_mutexattr_init(&shmp->att);
    pthread_mutexattr_setpshared(&shmp->att, PTHREAD_PROCESS_SHARED);

    // initialize shared memory
    file = fopen(outputFileName, "w");
    if (file == NULL)
    {
        fprintf(stderr, "Cant create new file for output.\n");
        shmdt(shmp);
        exit(1);
    }

    // initialize mutexes
    if (pthread_mutex_init(&shmp->fileM, &shmp->att) != 0)
    {
        fclose(file);
        printf("mutex init failed\n");
        shmdt(shmp);
        exit(1);
    }

    if (pthread_mutex_init(&shmp->countM, &shmp->att) != 0)
    {
        fclose(file);
        pthread_mutex_destroy(&shmp->fileM);
        printf("mutex init failed\n");
        shmdt(shmp);
        exit(1);
    }
    // sets random generator of main process
    srand(time(0));
}

void atom2molecule(shmDataT *data, char type, uint id)
{
    printToFile(data, "%c %d: creating molecule %d\n", type, id, data->molecules);
    // wait for creation of molecule, factory will unlock it
    sem_wait(&data->creationWait);
    printToFile(data, "%c %d: finished creating molecule %d\n", type, id, data->molecules);
    // unlock for next atom, factory will lock it
    sem_post(&data->creationWait);
    // signal atom is in molecule
    sem_post(&data->facWait);
}

void oxygen(int seed, pid_t id, shmDataT *data)
{
    printToFile(data, "O %d: started\n", id);

    srand(seed);
    usleep(rand() % ti);
    printToFile(data, "O %d: going to queue\n", id);

    // lock counters, increment get copy of them, unlock
    pthread_mutex_lock(&data->countM);
    data->oxyWC++;
    if (data->hydWC >= HYC_C && data->oxyWC == OXY_C)
    {
        sem_post(&data->facWait);
    }
    pthread_mutex_unlock(&data->countM);

    sem_wait(&data->oxyWait);
    if (!data->done)
    {
        atom2molecule(data, 'O', id);
    }
    else
    {
        printToFile(data, "O %d: not enought H\n", id);
        sem_post(&data->oxyWait);
    }
}

void hydrogen(int seed, pid_t id, shmDataT *data)
{
    printToFile(data, "H %d: started\n", id);

    srand(seed);
    usleep(rand() % ti);
    printToFile(data, "H %d: going to queue\n", id);

    pthread_mutex_lock(&data->countM);
    data->hydWC++;
    if (data->hydWC == HYC_C && data->oxyWC >= OXY_C)
    {
        sem_post(&data->facWait);
    }
    pthread_mutex_unlock(&data->countM);

    sem_wait(&data->hydWait);
    if (!data->done)
    {
        atom2molecule(data, 'H', id);
    }
    else
    {
        printToFile(data, "H %d: not enought O or H\n", id);
        sem_post(&data->hydWait);
    }
}

void createMolecule(shmDataT *data)
{
    // increments molecule counter
    data->molecules++;
    data->hydWC -= HYC_C;
    data->oxyWC -= OXY_C;

    // triger creation of new molecule
    for (uint i = 0; i < HYC_C; i++)
        sem_post(&data->hydWait);
    for (uint i = 0; i < OXY_C; i++)
        sem_post(&data->oxyWait);

    // wait time required to create new molecule
    usleep(random() % tb);

    // unlock creationWait queue
    // finish creation of molecule
    sem_post(&data->creationWait);

    // checks if all molecules were created
    for (uint i = 0; i < HYC_C + OXY_C; i++)
        sem_wait(&data->facWait);

    // lock creationWait queue
    sem_wait(&data->creationWait);
}

void factory(shmDataT *data)
{
    while (!data->done)
    {
        if (data->molecules * HYC_C + HYC_C > nh || data->molecules * OXY_C + OXY_C > no)
        {
            // not enough atoms for next molecule
            data->done = true;
            // free all remaining atoms
            sem_post(&data->hydWait);
            sem_post(&data->oxyWait);
            return;
        }

        // wait for factory to be woken up
        sem_wait(&data->facWait);
        // lock counters
        pthread_mutex_lock(&data->countM);

        // while there enought atoms to create molecule create molecules
        while (data->hydWC >= HYC_C && data->oxyWC >= OXY_C)
        {
            createMolecule(data);
        }

        // checks if there are enough atoms for next molecule
        pthread_mutex_unlock(&data->countM);
    }
}

int main(int argc, char **argv)
{
    // argument processing part
    processArgs(argc, argv);

    runInit();

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
            shmdt(shmp);
            exit(EXIT_SUCCESS);
        }
        if (id < 0)
        { /* error occurred */
            fprintf(stderr, "Fork Failed");
            exit(EXIT_FAILURE);
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
            shmdt(shmp);
            exit(EXIT_SUCCESS);
        }
        if (id < 0)
        { /* error occurred */
            fprintf(stderr, "Fork Failed");
            exit(EXIT_FAILURE);
        }
    }

    // factory process
    shmDataT *shmp = shmat(shmid, NULL, 0);
    factory(shmp);

    // wait for all children to finish
    while (wait(NULL) > 0)
        ;

    fclose(file);
    des(shmp);
    exit(0);
}
