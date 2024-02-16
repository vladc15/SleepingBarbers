#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>


#include <stdio.h>
#include <semaphore.h>
#include <errno.h>
#include <stdatomic.h>

#define PROCESS_SHARED 1 /* pentru ca fiecare proces sa aiba acces la semafor */
#define TIMP_PENTRU_FREZAT 1
#define ADEVARAT 1
#define SEATS 3
#define BARBERS 2
#define CUSTOMERS 7

struct Monitor {
    sem_t mutex; // accesul la monitor
    sem_t *cond; // variabile conditionale
    atomic_int *count; // numarul de procese care asteapta pe variabila conditionala
    sem_t next; // blocheaza procesul curent cand dam signal altor procese
                // next.signal => reia executia un proces care fusese suspendat
    atomic_int next_count; // numarul de procese care asteapta sa preia accesul la monitor
    int n;

};

int init_Monitor(struct Monitor *monitor, int N) {
    if (sem_init(&monitor->mutex, PROCESS_SHARED, 1)) {
        perror(NULL);
        return errno;
    }

    monitor->cond = ((sem_t*)mmap(0, N * sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    monitor->count = ((atomic_int*)mmap(0, N * sizeof(atomic_int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    
    monitor->n = N;
    if (sem_init(&monitor->next, PROCESS_SHARED, 0)) {
        perror(NULL);
        return errno;
    }
    //monitor->next_count = 0;
    //monitor->next_count = ATOMIC_VAR_INIT(0);
    monitor->next_count = ATOMIC_VAR_INIT(0);
    for (int i = 0; i < N; i++) {
        if (sem_init(&monitor->cond[i], PROCESS_SHARED, 0)) {
            perror(NULL);
            return errno;
        }
        //monitor->count[i] = 0;
        monitor->count[i] = ATOMIC_VAR_INIT(0);
    }
    return 0;
}

void enter_Monitor(struct Monitor *monitor) {
    //atomic_fetch_add(&monitor->next_count, 1);
    sem_wait(&monitor->mutex);
}

void wait_Monitor(struct Monitor *monitor, int ind) {
    //monitor->count[ind]++;
    atomic_fetch_add(&monitor->count[ind], 1);
    
    if (atomic_load(&monitor->next_count) > 0) {
        //monitor->next_count--; // ramane neschimbat pentru ca procesul curent preia locul procesului care va prelua accesul la monitor
        sem_post(&monitor->next); // in coada de asteptare a proceselor care coexista in monitor
    } 
    else {
        sem_post(&monitor->mutex);
    }
    sem_wait(&monitor->cond[ind]);
    
    //monitor->count[ind]--;
    atomic_fetch_sub(&monitor->count[ind], 1);
}

int signal_Monitor(struct Monitor *monitor, int ind) {
    if (atomic_load(&monitor->count[ind]) > 0) { // doar daca exista procese care asteapta pe variabila conditionala
        //monitor->next_count++;     // dam signal; altfel, nu, pentru ca variabilele conditionale nu au istoric
        atomic_fetch_add(&monitor->next_count, 1);
        sem_post(&monitor->cond[ind]); // dar semafoarele da
        sem_wait(&monitor->next);
        
        //monitor->next_count--;
        atomic_fetch_sub(&monitor->next_count, 1);

        return 0;
    }
    else {
        return 1;
    }
}

// in problema SleepingBarbers nu avem nevoie de broadcast
void broadcast_Monitor(struct Monitor *monitor, int ind) {
    if (atomic_load(&monitor->count[ind]) > 0) {
        //monitor->next_count += monitor->count[ind];
        atomic_fetch_add(&monitor->next_count, atomic_load(&monitor->count[ind]));
        for (int i = 0; i < monitor->count[ind]; i++)
            sem_post(&monitor->cond[ind]);
        for (int i = 0; i < monitor->count[ind]; i++)
            sem_wait(&monitor->next);
        //monitor->next_count -= monitor->count[ind];
        atomic_fetch_sub(&monitor->next_count, atomic_load(&monitor->count[ind]));
    }
}

void exit_Monitor(struct Monitor *monitor) {
    sem_post(&monitor->mutex);
}

void destroy_Monitor(struct Monitor *monitor) {
    sem_destroy(&monitor->mutex);

    for (int i = 0; i < monitor->n; i++)
        sem_destroy(&monitor->cond[i]);
    sem_destroy(&monitor->next);
    munmap(monitor->cond, monitor->n * sizeof(sem_t));
    munmap(monitor->count, monitor->n * sizeof(atomic_int));
}

void continue_Monitor(struct Monitor *monitor) {
    if (atomic_load(&monitor->next_count) > 0) {
        //monitor->next_count--;
        sem_post(&monitor->next);
    }
    else {
        sem_post(&monitor->mutex);
    }
}

struct SleepingBarbers {
    int freeSeats;
    pid_t customerPids[SEATS];
    pid_t barberPids[SEATS];
    int nextCustomer;
    int nextSeat;
};

void init_SleepingBarbers(struct SleepingBarbers *sb) {
    sb->freeSeats = SEATS;
    sb->nextCustomer = sb->nextSeat = 0;
}

void barber(struct Monitor *monitor, struct SleepingBarbers *sb) {
    enter_Monitor(monitor);
    int myNext;
    pid_t customerPid;
    	
	printf("Barber %d joins shop\n", getpid());
	while(ADEVARAT){
	
		printf("Barber %d goes to sleep\n", getpid());

        wait_Monitor(monitor, 0);

		myNext = sb->nextCustomer; // Luam urmatorul Customer
		sb->nextCustomer = ((sb->nextCustomer) + 1) % SEATS; // Setam urmatorul Customer care sa fie servit
		customerPid = sb->customerPids[myNext]; // Luam PID-ul Customer
		sb->barberPids[myNext] = getpid(); // Punem PID-ul pentru Customer
		
        signal_Monitor(monitor, 1);
		
        continue_Monitor(monitor);
        printf("Barber %d is serving the customer %d\n", getpid(), customerPid);
        //sleep(1);
		printf("Barber %d finished cutting the hair for customer %d\n", getpid(), customerPid);

	}
}

void customer(struct Monitor *monitor, struct SleepingBarbers *sb) {
    enter_Monitor(monitor);    
    int mySeat;
    pid_t barberPid;
    printf("Customer %d enters shop\n", getpid());
    if((sb->freeSeats) > 0){ // Verificam daca sunt locuri libere
        --(sb->freeSeats); // Scadem numarul de locuri libere
        printf("Customer %d sits in waiting room.\n", getpid()); 
        mySeat = sb->nextSeat; // Luam locul pe care trebuie sa ne asezam
        sb->nextSeat = ((sb->nextSeat) + 1) % SEATS; // Setam urmatorul loc liber
        sb->customerPids[mySeat] = getpid(); // Punem PID-ul pentru Barber

        signal_Monitor(monitor, 0);

        wait_Monitor(monitor, 1);

        barberPid = sb->barberPids[mySeat]; // Luam PID-ul Barber
        (sb->freeSeats)++; // Incrementam numarul de locuri de asteptare libere
        
        continue_Monitor(monitor);

        printf("Customer %d is having hair cut by barber %d.\n", getpid(), barberPid);
        
    }
    else{
        exit_Monitor(monitor);
        //continue_Monitor(monitor);
        printf("Customer %d finds no seat and leaves.\n",getpid()); // Nu au fost gasite locuri libere
    
    }
}


int main(int argc, char *argv[]) {

    

    char shm_name1 [] = "SB_sb";
	int shm_fd1;
	shm_fd1 = shm_open ( shm_name1 , O_CREAT | O_RDWR , S_IRUSR | S_IWUSR );
	if (shm_fd1 < 0) {
		perror ( NULL );
		return errno ;
	}

    char shm_name2 [] = "SB_m";
	int shm_fd2;
	shm_fd2 = shm_open ( shm_name2 , O_CREAT | O_RDWR , S_IRUSR | S_IWUSR );
	if (shm_fd2 < 0) {
		perror ( NULL );
		return errno ;
	}

    

	//size_t shm_size = (sizeof(struct Monitor) + sizeof(struct SleepingBarbers) + 2*sizeof(int)) + (4096 - (sizeof(struct Monitor) + sizeof(struct SleepingBarbers) + 2*sizeof(int))%4096);
    size_t shm_size1 = sizeof(struct SleepingBarbers);
    size_t shm_size2 = sizeof(struct Monitor);

	if ( ftruncate ( shm_fd1 , shm_size1 ) == -1) {
		perror ( NULL );
		shm_unlink ( shm_name1 );
		return errno ;
	}

    if ( ftruncate ( shm_fd2 , shm_size2 ) == -1) {
        perror ( NULL );
        shm_unlink ( shm_name2 );
        return errno ;
    }

	pid_t pids[BARBERS + CUSTOMERS + 3];

	struct Monitor *shm_ptr_init_monitor;
    struct SleepingBarbers *shm_ptr_init_sb;
	
    shm_ptr_init_sb = ((struct SleepingBarbers*)mmap (0 , sizeof(struct SleepingBarbers) , PROT_READ | PROT_WRITE , MAP_SHARED , shm_fd1 , 0 ));
    
    if ( shm_ptr_init_sb == MAP_FAILED ) {
        
        perror ( NULL );
        shm_unlink ( shm_name1 );
        return errno ;
    }

    shm_ptr_init_monitor = mmap (0 , sizeof(struct Monitor) , PROT_READ | PROT_WRITE , MAP_SHARED , shm_fd2 , 0 );
	if ( shm_ptr_init_monitor == MAP_FAILED ) {
		printf("BARBERS: %d\n", BARBERS);
        perror ( NULL );
		shm_unlink ( shm_name2 );
		return errno ;
	}
    
    
        


	if (init_Monitor(shm_ptr_init_monitor, 2) != 0) { // 2 variabile conditionale
        perror(NULL);
        return errno;
    }

    init_SleepingBarbers(shm_ptr_init_sb);

    
    for (int i = 1; i <= BARBERS; i++){
        pids[i] = fork();
        if(pids[i] < 0)
            return errno;
        else
            if(pids[i] == 0){
                struct Monitor *shm_ptr_monitor;
                shm_ptr_monitor = mmap (0 , sizeof(struct Monitor) , PROT_READ | PROT_WRITE , MAP_SHARED , shm_fd2 , 0 );
                if ( shm_ptr_monitor == MAP_FAILED ) {
                    perror ( NULL );
                    shm_unlink ( shm_name2 );
                    return errno ;
                }
                struct SleepingBarbers *shm_ptr_sb;
                shm_ptr_sb = mmap (0 , sizeof(struct SleepingBarbers) , PROT_READ | PROT_WRITE , MAP_SHARED , shm_fd1 , 0 );
                if ( shm_ptr_sb == MAP_FAILED ) {
                    perror ( NULL );
                    shm_unlink ( shm_name1 );
                    return errno ;
                }
                barber(shm_ptr_monitor, shm_ptr_sb);
                munmap(shm_ptr_monitor, sizeof(struct Monitor));
                munmap(shm_ptr_sb, sizeof(struct SleepingBarbers));

                exit(EXIT_SUCCESS);
            }
    }
    

    sleep(1);
    	
    for (int i = 1; i <= CUSTOMERS; i++) {
        pids[BARBERS + i] = fork();

        if (pids[BARBERS + i] < 0)
            return errno;
        else
            if (pids[BARBERS + i] == 0) {

                struct Monitor *shm_ptr_monitor;
                shm_ptr_monitor = mmap (0 , sizeof(struct Monitor) , PROT_READ | PROT_WRITE , MAP_SHARED , shm_fd2 , 0 );
                if ( shm_ptr_monitor == MAP_FAILED ) {
                    perror ( NULL );
                    shm_unlink ( shm_name2 );
                    return errno ;
                }
                struct SleepingBarbers *shm_ptr_sb;
                shm_ptr_sb = mmap (0 , sizeof(struct SleepingBarbers) , PROT_READ | PROT_WRITE , MAP_SHARED , shm_fd1 , 0 );
                if ( shm_ptr_sb == MAP_FAILED ) {
                    perror ( NULL );
                    shm_unlink ( shm_name1 );
                    return errno ;
                }

                customer(shm_ptr_monitor, shm_ptr_sb);

                munmap(shm_ptr_monitor, sizeof(struct Monitor));
                munmap(shm_ptr_sb, sizeof(struct SleepingBarbers));

                exit(EXIT_SUCCESS);
            }
	}

	/*for (int i = 1; i <= CUSTOMERS; i++)
		if (pids[BARBERS + i] != 0)
			wait(NULL);*/
    for (int i=1; i<=CUSTOMERS; i++)
        if (pids[BARBERS + i] != 0) {
            kill(pids[BARBERS + i], SIGTERM);
            waitpid(pids[BARBERS + i], NULL, 0);
        }
	sleep(2);
    for (int i=1; i<=BARBERS; i++)
		if (pids[i] != 0) {
            kill(pids[i], SIGTERM);
            waitpid(pids[i], NULL, 0);
        }

    

	destroy_Monitor(shm_ptr_init_monitor);
    munmap(shm_ptr_init_sb, sizeof(struct SleepingBarbers));
    munmap(shm_ptr_init_monitor, sizeof(struct Monitor));
	shm_unlink(shm_name1);
    shm_unlink(shm_name2);

    return 0;
}