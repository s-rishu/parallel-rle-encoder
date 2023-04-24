/*References:
1. https://www.geeksforgeeks.org/c-program-to-read-contents-of-whole-file/
2. https://en.wikipedia.org/wiki/Pthreads
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <signal.h>


#define MAX_SIZE 260000
//#define MAX_SIZE 10

struct task {
    char *start; //start address of string to be encoded
    int    size; //size of the string to be encoded in bytes
    int  seq_no; //sequence number of task
};
struct result {
    char *start; //start address of the result
    int    size; //size of the string 
};
//global variables
struct task task_q[MAX_SIZE];
struct result* all_result[MAX_SIZE] = {NULL};
int task_q_head = 0;
int task_q_tail = -1;
int total_task = 0;

//mutex and condition variables to protect task queue
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;

//mutex and condition variables to protect result queue
pthread_mutex_t mutex_res = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t new_res[MAX_SIZE] = {PTHREAD_COND_INITIALIZER};

//function to produce task;
void produce(struct task t){
    //printf("in produce\n");
    pthread_mutex_lock(&mutex); //grab lock
    task_q[task_q_tail+1] = t;  //insert task
    task_q_tail++;
    total_task++;
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mutex);
}

//function to consume task;
struct task consume(){
    pthread_mutex_lock(&mutex); //grab lock
    while(total_task == 0){
        pthread_cond_wait(&not_empty, &mutex);
    }
    //printf("proceeded in consume\n");
    total_task--;
    struct task t = task_q[task_q_head];  //grab task
    task_q_head++;
    pthread_mutex_unlock(&mutex);
    return t;
}

struct result* encode(char *start, int size){
    //printf("In encode()\n");
    char prev_char;
    char curr_char;
    char count;
    char* out = malloc(2*size*sizeof(char));
    int idx = 0;
    struct result* r = malloc(sizeof(struct result));
    int i = 0;
    prev_char = *(start+i);
    count = 1;
    for(i=1; i<size; i++){
        //printf("in encode for");
        curr_char = *(start+i);
        if(curr_char == prev_char){
            count++;
        }
        else{
            *(out+idx) = prev_char;
            *(out+idx+1) = count;
            idx = idx+2;
            count = 1;
            prev_char = curr_char;
        }
    };
    *(out+idx) = prev_char;
    *(out+idx+1) = count;
    r->start = out;
    r->size = idx+2;
    return r;
}

void *process_tasks(void *args){
    //printf("in process tasks \n");
    while(1){ 
        //pthread_mutex_lock(&mutex); //grab lock
        struct task t = consume();
        struct result* r = encode(t.start, t.size); //update result queue

        pthread_mutex_lock(&mutex_res); //grab lock
        all_result[t.seq_no] = r;
        pthread_cond_signal(&new_res[t.seq_no]);
        pthread_mutex_unlock(&mutex_res);
    }
    return NULL;
}

void stitch_results(int res_count){
    int i = 0;
    pthread_mutex_lock(&mutex_res); //grab lock
    while(!all_result[i]){
        pthread_cond_wait(&new_res[i], &mutex_res);
    }
    pthread_mutex_unlock(&mutex_res); 

    int len = all_result[i]->size;
    fwrite(all_result[i]->start, sizeof(char), len-2, stdout);

    for(i=1; i<res_count; i++){
        pthread_mutex_lock(&mutex_res); //grab lock
        while(!all_result[i]){
            pthread_cond_wait(&new_res[i], &mutex_res);
        }
        pthread_mutex_unlock(&mutex_res);
        if(*(all_result[i]->start) == *(all_result[i-1]->start+len-2)){
            *(all_result[i]->start+1) = *(all_result[i]->start+1) + *(all_result[i-1]->start+len-1);
        }
        else{
            fwrite(all_result[i-1]->start+len-2, sizeof(char), 2, stdout);
        }        
        len = all_result[i]->size;
        fwrite(all_result[i]->start, sizeof(char), len-2, stdout);
    }
    fwrite(all_result[i-1]->start+len-2, sizeof(char), 2, stdout);  
}

int main(int argc, char **argv) {
    //get job count
    int job_count = 0;
    int opt;
    while((opt = getopt(argc, argv, "j:")) != -1){
        switch (opt){
            case 'j':
                job_count = atoi(optarg);
                break;
            default:
                abort();
        }
    }
    
    if (job_count > 0){
        //parallel encoding
        //create thread pool
        pthread_t threads[job_count];
        int thread_args[job_count];
        int res_code;
        for(int i=0; i<job_count; i++){
            thread_args[i] = i;
            res_code = pthread_create(&threads[i], NULL, process_tasks, &thread_args[i]);
            assert(!res_code);
        }
        //create tasks, associate a sequence number, add them to the pool
        int task_no = 0;
        for(int i = optind; i < argc; i++){
            // Open file
            //printf("reading files\n");
            int fd = open(argv[i], O_RDONLY);
            if (fd == -1){
                printf("Error reading file: %s", argv[i]);
                exit(EXIT_SUCCESS);
            };
            //printf("progressed reading files\n");
            // Get file size
            struct stat sb;
            if (fstat(fd, &sb) == -1){
                printf("Error getting file stats: %s", argv[i]);
                exit(EXIT_SUCCESS);
            };
            //printf("mapping file to memory\n");
            // Map file into memory
            char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (addr == MAP_FAILED){
                printf("Error mapping file: %s", argv[i]);
                exit(EXIT_SUCCESS);
            };
            size_t file_size = sb.st_size;
            //divide file into tasks
            //printf("creating tasks \n");
            while(file_size>0){   //TODO: check calculation
                struct task t;
                if(file_size>=4000){
                    t.start = addr;
                    t.size = 4000;
                    t.seq_no = task_no;
                    addr = addr + 4000;
                    task_no++;
                    file_size = file_size-4000;
                }
                else{
                    t.start = addr;
                    t.size = file_size;
                    t.seq_no = task_no;
                    addr = addr + file_size;
                    task_no++;
                    file_size = 0;
                }
                produce(t);
            };
        };
        //combine results in the result pool
        stitch_results(task_no);
    }
    else{
        //serial encoding
        int seq_no = 0;
        for(int i = optind; i < argc; i++){

            // Open file
            int fd = open(argv[i], O_RDONLY);
            if (fd == -1){
                printf("Error reading file: %s", argv[i]);
                exit(EXIT_SUCCESS);
            }
            // Get file size
            struct stat sb;
            if (fstat(fd, &sb) == -1){
                printf("Error getting file stats: %s", argv[i]);
                exit(EXIT_SUCCESS);
            }

            // Map file into memory
            char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0); //TODO:need to unmap
            if (addr == MAP_FAILED){
                printf("Error mapping file: %s", argv[i]);
                exit(EXIT_SUCCESS);
            }
            all_result[seq_no] = encode(addr, sb.st_size);
            seq_no++;
        }
        stitch_results(seq_no);
   
    return 0;
}
}
