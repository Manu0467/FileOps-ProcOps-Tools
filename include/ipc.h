#pragma once

#include <semaphore.h>
#include <sys/types.h>

#define MAX_WORKERS 32    
#define MAX_JOBS 66000    
#define MAX_RESULTS 4096   
#define DB_PART_COMPLETE 0   
#define DB_FULL_COMPLETE 1


struct header_db{

    char magic[5];
    int32_t version;
    int32_t complete;
    int32_t file_record_count;
    int32_t worker_count;
};

struct Job {
    char dir_path[4096];
    int32_t current_depth;
};

struct FileRecord {
    char path[4096];
    long size;
    long mtime;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    char sha256[65];
};

struct WorkerStats {
    int32_t worker_id;
    pid_t pid;
    int32_t exit_status;
    int32_t jobs_processed;
    int32_t files_emitted;
    long bytes_emitted;
    long wall_time_ms;
    long user_cpu_us;
    long sys_cpu_us;
};

struct JobQueue {
    struct Job buffer[MAX_JOBS];
    int head; 
    int tail; 
    
    sem_t mutex;  
    sem_t items;  //cate elemente sunt in coada
    sem_t spaces; //cat loc liber mai este in coada
};

struct ResultChannel {
    struct FileRecord buffer[MAX_RESULTS];
    int head; 
    int tail; 
    
    sem_t mutex;
    sem_t items;  
    sem_t spaces; 
};


struct IPC_Memory {
    
    int32_t active_jobs;    
    int32_t max_depth;
    int32_t delay_ms;
    sem_t global_mutex;     
    
    struct JobQueue job_queue;
    struct ResultChannel result_channels[MAX_WORKERS];
    struct WorkerStats worker_stats[MAX_WORKERS];
};

