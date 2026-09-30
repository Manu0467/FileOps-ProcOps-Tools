#define _GNU_SOURCE //ca sa pot folosi CLOCK_MONOTONIC  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <dirent.h> 
#include <errno.h>
#include <signal.h>
#include "ipc.h"
#include "sha256.h"


volatile sig_atomic_t flag_sigterm = 0;

void handler_sigterm(int sig) 
{
    (void)sig;
    flag_sigterm = 1;
}

void calculeaza_sha256(const char *path, char *output_hex) {
  
    FILE *f = fopen(path, "rb");
    if (!f) { 
        output_hex[0] = '\0'; // daca fisierul nu poate fi citit stringul returnat va fi unul gol
        return; 
    }

    struct sha256 sha;
    sha256_init(&sha);
    
    unsigned char buffer[32768]; //citesc bucati din fisier
    int bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        sha256_append(&sha, buffer, bytes);
    }
    
    sha256_finalize_hex(&sha, output_hex);
    
    fclose(f);
}

void mesaj_control(int fd, const char* tip, const char* parametri) {
    if (fd == -1) return;
    
    char mesaj[PIPE_BUF];
    int lungime = snprintf(mesaj, sizeof(mesaj), "T5MSG type=%s %s\n", tip, parametri);
    if (lungime > 0 && lungime < sizeof(mesaj)) {
        write(fd, mesaj, lungime); 
    }
}

int main(int argc, char* argv[]){

//------------------partea de validare a argumentelor----------------------
  struct timespec start_time, end_time;
  clock_gettime(CLOCK_MONOTONIC, &start_time); //aici incep cronometrarea timpului total
  int worker_id = -1;
  char ipc_path[4096] = "";
  int control_fd = -1;
  char mesaj_pipe[512] = "";

  if (argc < 5)
  {
    snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d errno=%d where=worker_argc", worker_id, errno);
    mesaj_control(control_fd, "ERROR", mesaj_pipe);
    exit(1);
  }

  for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) {
          worker_id = atoi(argv[++i]);
      } else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) {
          strcpy(ipc_path, argv[++i]);
      } else if (strcmp(argv[i], "--control-fd") == 0 && i + 1 < argc) {
          control_fd = atoi(argv[++i]);
      }
  }
  signal(SIGTERM, handler_sigterm);
  signal(SIGPIPE, SIG_IGN);  //ignor si sigpipe pentru ca erorile de scriere sunt gestionate in cod
  //------------------------------------------------------------------------

  //-------------partea de maparea a ipc ului in memorie, din nou pentru ca procesul inceput prin execl il acopera pe celalt
  int fd_ipc = open(ipc_path, O_RDWR);
  if (fd_ipc == -1)
  {
    snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d errno=%d where=fd_ipc", worker_id, errno);
    mesaj_control(control_fd, "ERROR", mesaj_pipe);
    exit(2);
  }

  struct IPC_Memory *ipc = mmap(NULL, sizeof(struct IPC_Memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_ipc, 0); // e mult mai comod sa citesc si sa modific datele din fisierul mapat asa 
    if (ipc == MAP_FAILED) 
    {
        snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d errno=%d where=mmap", worker_id, errno);
        mesaj_control(control_fd, "ERROR", mesaj_pipe);
        exit(1);
    }
    close(fd_ipc);

    struct WorkerStats stats;
    memset(&stats, 0, sizeof(struct WorkerStats));
    stats.worker_id = worker_id;
    stats.pid = getpid();
    stats.exit_status = 1;
    int heartbeat_counter = 0; //pentru mesajele heartbeat

while (1)
{

        if (flag_sigterm) {
            printf("Worker %d: am primit SIGTERM, ies curat.\n", worker_id);
            snprintf(mesaj_pipe, sizeof(mesaj_pipe), "reason=sigterm jobs=%d files=%d", stats.jobs_processed, stats.files_emitted);
            mesaj_control(control_fd, "WORKER_EXITING", mesaj_pipe);
            break;
        }
        if (sem_trywait(&ipc->job_queue.items) == -1) //verific daca mai sunt elemente in coada, daca nu mai sunt atunci verific daca mai lucreaza vreun alt worker in paralel daca da atunci astept putin ca sa vad daca a mai adaugat vreun director in coada si dupa iau bucla de la capat
        {
            sem_wait(&ipc->global_mutex);    //si ca sa citesc daca mai sunt workeri care fac treaba, pun un lacat pe header kinda
            if (ipc->active_jobs == 0)
            {
                sem_post(&ipc->global_mutex);
                break;
            }
            sem_post(&ipc->global_mutex);

            usleep(10000);
            continue;
            
        }
        

    char dir_curent[2048]; //am pus lacat si pe coada si global pe structura pentru a fi sigura ca nu imi modifica alt worker datele pe care vreau sa le citesc
    int adancime_curenta;   
    sem_wait(&ipc->global_mutex);
    sem_wait(&ipc->job_queue.mutex);
    strcpy(dir_curent, ipc->job_queue.buffer[ipc->job_queue.head].dir_path);
    adancime_curenta = ipc->job_queue.buffer[ipc->job_queue.head].current_depth;
    ipc->job_queue.head = (ipc->job_queue.head + 1) % MAX_JOBS;
    sem_post(&ipc->global_mutex);
    sem_post(&ipc->job_queue.mutex);
    sem_post(&ipc->job_queue.spaces); //incrementez si nr de spatii libere din coada pentru ca tocmai am extras un fisier

    DIR * dd = opendir(dir_curent);

    if (dd == NULL)
    {
        sem_wait(&ipc->global_mutex); //daca un fisier/folder nu poate fi deschis o sa trecem peste ca sa nu se blocheze tot
        ipc->active_jobs--;
        sem_post(&ipc->global_mutex);
       
        snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d errno=%d where=opendir", worker_id, errno);
        mesaj_control(control_fd, "ERROR", mesaj_pipe);
        continue; 
    }else{

        struct dirent *de;
        while ((de = readdir(dd)) != NULL)
        {
            if (flag_sigterm) {
                break;
            }
        
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0){
                continue;
            }
            char full_path[4096];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_curent, de->d_name);

            struct stat s;
            if (lstat(full_path, &s) == -1)
            {
                snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d errno=%d where=struct_stat_worker", worker_id, errno);
                mesaj_control(control_fd, "ERROR", mesaj_pipe);
                continue;
            }

            if (S_ISDIR(s.st_mode)) //daca este director il adaug in coada
            {

                if (ipc->max_depth == -1 || adancime_curenta < ipc->max_depth)
                {

                    sem_wait(&ipc->global_mutex);
                    ipc->active_jobs++;
                    sem_post(&ipc->global_mutex);
                    
                    sem_wait(&ipc->job_queue.spaces); //scade nr de spatii libere
                    sem_wait(&ipc->job_queue.mutex); 
                    strcpy(ipc->job_queue.buffer[ipc->job_queue.tail].dir_path, full_path);
                    ipc->job_queue.buffer[ipc->job_queue.tail].current_depth = adancime_curenta + 1;
                    ipc->job_queue.tail = (ipc->job_queue.tail + 1) % MAX_JOBS;
                    sem_post(&ipc->job_queue.items); //creste nr de obiecte din coada
                    sem_post(&ipc->job_queue.mutex); 
                    
                }
    
            }
            else if (S_ISREG(s.st_mode))
            {
                struct FileRecord rec;
                memset(&rec, 0, sizeof(struct FileRecord));
                strcpy(rec.path, full_path);
                rec.uid = s.st_uid;
                rec.gid = s.st_gid;
                rec.size = s.st_size;
                rec.mode = s.st_mode;
                rec.mtime = s.st_mtime;
                calculeaza_sha256(full_path, rec.sha256);
                
                sem_wait(&ipc->result_channels[worker_id].spaces); //aici scriu datele in fisierul ipc, modific tail si foloses lacatul pe result_channels
                ipc->result_channels[worker_id].buffer[ipc->result_channels[worker_id].tail] = rec;
                ipc->result_channels[worker_id].tail = (ipc->result_channels[worker_id].tail + 1) % MAX_RESULTS;
                sem_post(&ipc->result_channels[worker_id].items);
                stats.files_emitted++;
                stats.bytes_emitted += s.st_size;
                if (ipc->delay_ms > 0) 
                {
                    usleep(ipc->delay_ms * 1000); 
                }
            }
            
        }
        closedir(dd);
        
    }

    sem_wait(&ipc->global_mutex);
    ipc->active_jobs--;
    sem_post(&ipc->global_mutex);
    stats.jobs_processed++;

    snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d jobs=%d files=%d bytes=%ld", worker_id, stats.jobs_processed, stats.files_emitted, stats.bytes_emitted);
    mesaj_control(control_fd, "JOB_DONE", mesaj_pipe);

    heartbeat_counter++;
    if (heartbeat_counter % 10 == 0) {
        snprintf(mesaj_pipe, sizeof(mesaj_pipe), "worker_id=%d", worker_id);
        mesaj_control(control_fd, "HEARTBEAT", mesaj_pipe);
    }
}

clock_gettime(CLOCK_MONOTONIC, &end_time);

stats.wall_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

struct rusage usage;
getrusage(RUSAGE_SELF, &usage);
stats.user_cpu_us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec ;
stats.sys_cpu_us = usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec ;
stats.exit_status = 0; //daca programul ajunge pana aici atunci inseamna ca executia s-a terminat cu succes
ipc->worker_stats[worker_id] = stats;

 if (!flag_sigterm) 
 {
    snprintf(mesaj_pipe, sizeof(mesaj_pipe), "reason=finished jobs=%d files=%d", stats.jobs_processed, stats.files_emitted);
    mesaj_control(control_fd, "WORKER_EXITING", mesaj_pipe);
 }

   return 0;
}