#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <semaphore.h>
#include <time.h>
 
// Defines for adding variables into shared memory
#define MMAP(pointer) {(pointer) = mmap(NULL, sizeof(*(pointer)), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);}
#define MUNMAP(pointer) {munmap((pointer), sizeof((pointer)));}

// Shared memory variables struct
typedef struct sharedVars {
    int lineCount; // Counter of number of lines
    int oxyCount; // Counter of oxygen atoms
    int hydCount; // Counter of hydrogen atoms
    int molCount; // Number of molecules used when printing create
    int molCreatingCount; // Number of molecules used when printing creating
    int atomCount; // Counter of atoms
    int createCount; // Counter of printing create
    int creatingCount; // Counter of printing creating
    int queueCount; // Counter of atoms that joined the queue
    int totalMolecules; // Total molecules to be created
    int totalAtoms; // Total number of atoms
    int oxyAlone; // To check if oxygen is alone
} sharedVars;

// Shared memory semaphore struct
typedef struct semaphores {
    sem_t oxyQueue;
    sem_t hydQueue;
    sem_t mutex;
    sem_t mutexOut; // Output mutex
    sem_t mutexBar; // Barrier mutex
    sem_t turnstile;
    sem_t turnstile2;
    sem_t notEnough; // Semaphore for printing not enough ...
} semaphores;

sharedVars *shVars; // shVars with all the shared memory variables
semaphores *shSems; // shSems with all the shared semaphores

// File variable
FILE *pOUT;

int init(); // Run at the start of the program
void clean(); // Run at the end of the program
int isNumber(); // Check if string is a number
void printError(char errMsg[]); // Print error message
void printStarted(char atom, int atomId, sharedVars *data); // Print atom started message
void printQueue(char atom, int atomId, sharedVars *data); // Print atom going to queue message
void printCreating(char atom, int atomId, sharedVars *data); // Print creating molecule message
void printCreated(char atom, int atomId, sharedVars *data); // Print created molecule message
void printLackOH(char atom, int atomId, sharedVars *data); // Print not enough O or H message
void printLackH(char atom, int atomId, sharedVars *data); // Print not enough H message

// Main function
int main(int argc, char **argv) {

    // Argument variables
    int numberOxy;
    int numberHyd;
    int waitQueue;
    int waitCreate;

    init();    

    /********* FOR CHECKING INPUT ********/
    // If the number of arguments is not 5 (required), print an error
    if (argc != 5)
    {
        printError("Invalid arguments!");
    }
    else
    {
        // Check if they are all numbers, if not, print an error
        if (isNumber(argv[1]) && isNumber(argv[2]) && isNumber(argv[3]) && isNumber(argv[4]))
        {
            // Assign them to variables
            numberOxy = atoi(argv[1]);
            numberHyd = atoi(argv[2]);
            waitQueue = atoi(argv[3]);
            waitCreate = atoi(argv[4]);

            // Check if waitQueue and waitCreate are in the correct range, if not, print an error
            if (0 > waitQueue || waitQueue > 1000 || 0 > waitCreate || waitCreate > 1000)
            {
                printError("Invalid arguments!");
            }
        }
        else
        {
            printError("Invalid arguments!");
        }
    }

    // Count the total number of atoms
    shVars->totalAtoms = numberOxy + numberHyd;

    if (shVars->totalAtoms == 0)
    {
        printError("No atoms!");
    }

    // Count the total number of molecules that will be created
    for (int i = numberOxy, j = numberHyd; i > 0 && j > 1; i--, j-=2)
    {
        shVars->totalMolecules++;
    }

    // Check if no molecule can be created and post according semaphores
    if (numberHyd == 0 || numberHyd == 1)
    {
        for (int i = 0; i < numberOxy; i++)
        {
            sem_post(&shSems->oxyQueue);
            sem_post(&shSems->mutexBar);
            sem_post(&shSems->mutexOut);
        }
        shVars->oxyAlone = 1;
    }

    /******** ACTUAL PROGRAM ********/

    // For loop for oxygen child processes
    for (int i = 1; i <= numberOxy; i++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            printError("Fork function failure!");
        }
        else if (pid == 0)
        {
            
            printStarted('O', i, shVars);

            // Code for releasing atoms to form a molecule
            sem_wait(&shSems->mutex);
            shVars->oxyCount += 1;

            if (shVars->hydCount >= 2)
            {
                sem_post(&shSems->hydQueue);
                sem_post(&shSems->hydQueue);
                shVars->hydCount -= 2;
                sem_post(&shSems->oxyQueue);
                shVars->oxyCount -= 1;
            }
            else
            {
                sem_post(&shSems->mutex);
            }
            
            // Wait before joining queue
            srand(getpid());
            int waitTime = (rand() % (waitQueue + 1));
            waitTime *= 1000;

            usleep(waitTime);

            printQueue('O', i, shVars);

            sem_wait(&shSems->oxyQueue);

            // Barrier code
            sem_wait(&shSems->mutexBar);
                
                // Check if atom can form a molecule, if not, print out accordingly and exit
                int willExit = 0;
                
                if (shVars->oxyAlone)
                {
                    if (shVars->queueCount == shVars->totalAtoms)
                    {
                        // If all atoms already joined the queue, unlock the notEnough semaphore
                        for (int i = 0; i < shVars->queueCount; i++)
                        {
                            sem_post(&shSems->notEnough);
                        }
                    }
                    // Wait here until all atoms joined the queue and then release the semaphores needed, print out and exit
                    sem_wait(&shSems->notEnough);
                    sem_wait(&shSems->mutexOut);
                    printLackH('O', i, shVars);
                    sem_post(&shSems->oxyQueue);
                    sem_post(&shSems->mutexBar);
                    sem_post(&shSems->mutexOut);
                    willExit = 1;
                }

                if (willExit)
                {
                    exit(0);
                }

                printCreating('O', i, shVars);

                // Oxygen should wait for random time before creating the molecule
                srand(getpid());
                waitTime = (rand() % (waitCreate + 1));
                waitTime *= 1000;
                usleep(waitTime);

                shVars->atomCount += 1;
                if (shVars->atomCount == 3)
                {
                    sem_post(&shSems->turnstile);
                    sem_post(&shSems->turnstile);
                    sem_post(&shSems->turnstile);
                }

            sem_post(&shSems->mutexBar);

            sem_wait(&shSems->turnstile);

            sem_wait(&shSems->mutexBar);
                shVars->atomCount -= 1;
                if (shVars->atomCount == 0)
                {
                    sem_post(&shSems->turnstile2);
                    sem_post(&shSems->turnstile2);
                    sem_post(&shSems->turnstile2);
                }
            sem_post(&shSems->mutexBar);

            sem_wait(&shSems->turnstile2);
            // End of barrier code
        
            sem_post(&shSems->mutex);

            printCreated('O', i, shVars);

            exit(0);
        }
    }

    // For loop for hydrogen child processes
    for (int i = 1; i <= numberHyd; i++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            printError("Fork function failure!");
        }
        else if (pid == 0)
        {
            printStarted('H', i, shVars);

            sem_wait(&shSems->mutex);
            shVars->hydCount += 1;
            
            // Code for releasing atoms to form a molecule
            if (shVars->hydCount >= 2 && shVars->oxyCount >= 1)
            {
                sem_post(&shSems->hydQueue);
                sem_post(&shSems->hydQueue);
                shVars->hydCount -= 2;
                sem_post(&shSems->oxyQueue);
                shVars->oxyCount -= 1;
            }
            else
            {
                sem_post(&shSems->mutex);
            }

            // Wait before joining the queue
            srand(getpid());
            int waitTime = (rand() % (waitQueue + 1));
            waitTime *= 1000;
            
            usleep(waitTime);

            printQueue('H', i, shVars);

            // Check if atom can form a molecule, if not, print out accordingly and exit
            int willExit = 0;

            if (shVars->molCount > shVars->totalMolecules)
            {
                // If all atoms already joined the queue, unlock the notEnough semaphore
                if (shVars->queueCount == shVars->totalAtoms)
                {
                    for (int i = 0; i < shVars->queueCount; i++)
                    {
                        sem_post(&shSems->notEnough);
                    }
                }
                // Wait here until all atoms joined the queue and then release the semaphores needed, print out and exit
                sem_post(&shSems->oxyQueue);
                sem_wait(&shSems->notEnough);
                sem_wait(&shSems->mutexOut);
                printLackOH('H', i, shVars);
                sem_post(&shSems->oxyQueue);
                sem_post(&shSems->mutexOut);
                sem_post(&shSems->mutexOut);
                willExit = 1;
            }

            if (willExit)
            {
                exit(0);
            }

            sem_wait(&shSems->hydQueue);

            // Barrier code
            sem_wait(&shSems->mutexBar);
                printCreating('H', i, shVars);
                shVars->atomCount += 1;
                if (shVars->atomCount == 3)
                {
                    sem_post(&shSems->turnstile);
                    sem_post(&shSems->turnstile);
                    sem_post(&shSems->turnstile);
                }
            sem_post(&shSems->mutexBar);

            sem_wait(&shSems->turnstile);

            sem_wait(&shSems->mutexBar);
                shVars->atomCount -= 1;
                if (shVars->atomCount == 0)
                {
                    sem_post(&shSems->turnstile2);
                    sem_post(&shSems->turnstile2);
                    sem_post(&shSems->turnstile2);
                }
            sem_post(&shSems->mutexBar);

            sem_wait(&shSems->turnstile2);
            // End of barrier code

            printCreated('H', i, shVars);

            exit(0);
        }
    }

    for (int i = 0; i < numberOxy + numberHyd; i++) {
        wait(NULL);
    }

    clean();
}

// FUNCTIONS
// Initialize
int init()
{
    // Open output file
    pOUT = fopen("proj2.out", "w");

    // Map shared memory
    MMAP(shVars);
    MMAP(shSems);

    // Initialize shared variables
    shVars->hydCount = 0;
    shVars->oxyCount = 0;
    shVars->lineCount = 1;
    shVars->molCount = 1;
    shVars->molCreatingCount = 1;
    shVars->atomCount = 0;
    shVars->createCount = 0;
    shVars->creatingCount = 0;
    shVars->queueCount = 0;
    shVars->totalMolecules = 0;
    shVars->totalAtoms = 0;
    shVars->oxyAlone = 0;


    // Initialize semaphores
    sem_init(&shSems->oxyQueue, 1, 0);
    sem_init(&shSems->hydQueue, 1, 0);
    sem_init(&shSems->mutex, 1, 1);
    sem_init(&shSems->mutexOut, 1, 1);
    sem_init(&shSems->mutexBar, 1, 1);
    sem_init(&shSems->turnstile, 1, 0);
    sem_init(&shSems->turnstile2, 1, 1);
    sem_init(&shSems->notEnough, 1, 0);

    return 0;
}

// Clean
void clean()
{
    // Destroy semaphores
    sem_destroy(&shSems->oxyQueue);
    sem_destroy(&shSems->hydQueue);
    sem_destroy(&shSems->mutex);
    sem_destroy(&shSems->mutexOut);
    sem_destroy(&shSems->mutexBar);
    sem_destroy(&shSems->turnstile);
    sem_destroy(&shSems->turnstile2);
    sem_destroy(&shSems->notEnough);

    // Unmap shared memory
    MUNMAP(shSems);
    MUNMAP(shVars);

    // If file exists, close it
    if (pOUT != NULL)
    {
        fclose(pOUT);
    }
}

// Check if string is a number
int isNumber(char s[])
{
    if (strcmp(s, "") == 0) 
    {
        return 0;
    }
    for (int i = 0; s[i]; i++)
    {
        if (!isdigit(s[i]))
        {
            return 0;
        }
    }
    return 1;
}

// Functions
void printError(char errMsg[])
{
    fprintf(stderr, "%s\n", errMsg);
    clean();
    exit(1);
}

void printStarted(char atom, int atomId, sharedVars *data)
{
    sem_wait(&shSems->mutexOut);
        fprintf(pOUT, "%d: %c %d: started\n", data->lineCount++, atom, atomId);
        fflush(pOUT);
    sem_post(&shSems->mutexOut);
}

void printQueue(char atom, int atomId, sharedVars *data)
{
    sem_wait(&shSems->mutexOut);
        fprintf(pOUT, "%d: %c %d: going to queue\n", data->lineCount++, atom, atomId);
        fflush(pOUT);
        data->queueCount++;
    sem_post(&shSems->mutexOut);
}

void printCreating(char atom, int atomId, sharedVars *data)
{
    sem_wait(&shSems->mutexOut);
        fprintf(pOUT, "%d: %c %d: creating molecule %d\n", data->lineCount++, atom, atomId, data->molCreatingCount);
        fflush(pOUT);
        data->creatingCount++;
        // Update molCreatingCount accordingly
        if (data->creatingCount == 3)
        {
            data->creatingCount = 0;
            data->molCreatingCount++;
        }

    sem_post(&shSems->mutexOut);
}

void printCreated(char atom, int atomId, sharedVars *data)
{
    sem_wait(&shSems->mutexOut);
        fprintf(pOUT, "%d: %c %d: molecule %d created\n", data->lineCount++, atom, atomId, data->molCount);
        fflush(pOUT);
        data->createCount++;
        // Update molCount accordingly
        if (data->createCount == 3)
        {
            data->createCount = 0;
            data->molCount++;
        }
        if (data->molCount > data->totalMolecules)
        {
            data->oxyAlone = 1;
            sem_post(&shSems->oxyQueue);
            sem_post(&shSems->mutexOut);
        }

        sem_post(&shSems->mutexOut);
}

void printLackOH(char atom, int atomId, sharedVars *data)
{
    //sem_wait(&shSems->mutexOut);
        fprintf(pOUT, "%d: %c %d: not enough O or H\n", data->lineCount++, atom, atomId);
        fflush(pOUT);
    //sem_post(&shSems->mutexOut);
}

void printLackH(char atom, int atomId, sharedVars *data)
{
    sem_wait(&shSems->mutexOut);
        fprintf(pOUT, "%d: %c %d: not enough H\n", data->lineCount++, atom, atomId);
        fflush(pOUT);
    sem_post(&shSems->mutexOut);
}