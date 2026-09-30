#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include "ipc.h"
#include "sha256.h"

volatile sig_atomic_t flag_status   = 0;
volatile sig_atomic_t flag_shutdown = 0;
volatile sig_atomic_t flag_sigchld  = 0;

void handler_sigusr1(int sig) {
    flag_status = 1;
}

void handler_shutdown(int sig) {
    flag_shutdown = 1;
}
void handler_sigchld(int sig) {
    flag_sigchld = 1;
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

void print_status(struct IPC_Memory *ipc, int nr_workers, pid_t *worker_pids, int files_collected, int complete) {

    int alive = 0;
    for (int i = 0; i < nr_workers; i++)
    {
        if (worker_pids[i] != 0) alive++;
    }

    int joburi_coada = 0;
    sem_getvalue(&ipc->job_queue.items, &joburi_coada);
    int active = ipc->active_jobs;

    long total_bytes = 0;
    for (int i = 0; i < nr_workers; i++)
    {
        total_bytes += ipc->worker_stats[i].bytes_emitted;
    }

    printf("STATUS queued_jobs=%d active_jobs=%d files=%d bytes=%ld workers_alive=%d complete=%d\n", joburi_coada, active, files_collected, total_bytes, alive, complete);
    fflush(stdout); //chestia asta imi goleste bufferul de la printf instant, adica imi afiseaza imediat textul de la status fara a mai astepta sa se umple bufferul
}

void graceful_shutdown(pid_t *worker_pids, int nr_workers, int graceful_timeout, struct IPC_Memory *ipc) {

    ipc->active_jobs = 0; //nu mai accept joburi noi in coada

    printf("MANAGER: Trimit SIGTERM workerilor...\n");
    for (int i = 0; i < nr_workers; i++) { //sigterm pentru workeri
        if (worker_pids[i] != 0) {
            kill(worker_pids[i], SIGTERM);
        }
    }

    int elapsed_ms = 0;
    int timeout_ms = graceful_timeout * 1000;

    while (elapsed_ms < timeout_ms) { //astept sa moara workerii
        int all_done = 1;
        for (int i = 0; i < nr_workers; i++) {
            if (worker_pids[i] != 0) {
                int status;
                pid_t p = waitpid(worker_pids[i], &status, WNOHANG); //WNOHANG nu blocheaza managerul daca un worker nu a murit inca
                if (p > 0) {
                    worker_pids[i] = 0;
                } else {
                    all_done = 0;
                }
            }
        }
        if (all_done) break;

        usleep(100000); 
        elapsed_ms += 100;
    }

    for (int i = 0; i < nr_workers; i++) {
        if (worker_pids[i] != 0) {
            printf("MANAGER: Worker %d nu a iesit la timp, trimit SIGKILL.\n", i);
            kill(worker_pids[i], SIGKILL); //sigkill nu poate fi ignorat
        }
    }

    for (int i = 0; i < nr_workers; i++) {
        if (worker_pids[i] != 0) {
            waitpid(worker_pids[i], NULL, 0);  //astept ca sigkill sa termine, aici are efect blocant
            worker_pids[i] = 0;
        }
    }
    printf("MANAGER: Toti workerii au fost opriti.\n");
}

void write_database(const char *db_path, struct IPC_Memory *ipc, int nr_workers, int files_collected, int complete_flag, const char *tmp_path) {
    
    struct header_db header;
    memset(&header, 0, sizeof(struct header_db));
    strcpy(header.magic, "INV4");
    header.version = 1;
    header.complete = complete_flag;  
    header.file_record_count = files_collected;
    header.worker_count = nr_workers;

    char tmp_db[4096];
    snprintf(tmp_db, sizeof(tmp_db), "%s.tmp", db_path);

    int fd_db = open(tmp_db, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_db == -1) {
        perror("Eroare la deschiderea fișierului DB temporar");
        return;
    }

    write(fd_db, &header, sizeof(struct header_db));

    int fd_tmp = open(tmp_path, O_RDONLY);
    if (fd_tmp != -1) {
        struct FileRecord rec;
        while (read(fd_tmp, &rec, sizeof(struct FileRecord)) == sizeof(struct FileRecord)) {
            write(fd_db, &rec, sizeof(struct FileRecord));
        }
        close(fd_tmp);
    }

    for (int i = 0; i < nr_workers; i++) //statisticile pentru workeri
    {
        write(fd_db, &ipc->worker_stats[i], sizeof(struct WorkerStats));
    }

    close(fd_db);

    if (rename(tmp_db, db_path) == -1) {
        perror("Eroare la rename DB");
    }
}


int main(int argc, char* argv[]){
    setvbuf(stdout, NULL, _IONBF, 0); //chestia asta dezactiveaza bufferingul ptr printf, in script nu imi gasea mesajele de status

    char root_dir[4096]="";
    int nr_workers=0;
    char ipc_path[4096]="";
    char db_path[4096]="";
    char pid_file_path[4096]="";
    int max_depth = -1;
    int simulate_work_ms = 0;
    int verify = 0;
    int dump = 0;
    int graceful_timeout = 10; //implicit 10 secunde
    strcpy(ipc_path, "data/ipc.mmap");
    strcpy(db_path, "data/inventory.db");

//---------------partea de validare a argumentelor-------------------------
   


if (argc < 2)
{
        perror("Nu ati introdus niciun argument!");
        exit(1);
}

 for (int i = 1; i < argc; i++) 
    {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) strcpy(root_dir, argv[++i]);
        else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) nr_workers = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) strcpy(ipc_path, argv[++i]);
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) strcpy(db_path, argv[++i]);
        else if (strcmp(argv[i], "--simulate-work-ms") == 0 && i + 1 < argc) simulate_work_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc) max_depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--graceful-timeout") == 0 && i + 1 < argc) graceful_timeout = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) strcpy(pid_file_path, argv[++i]);
        else if (strcmp(argv[i], "--verify") == 0) verify = 1;
        else if (strcmp(argv[i], "--dump") == 0) dump = 1;
    }
    

if (verify)
{
        int fd = open(db_path, O_RDONLY);
        if (fd == -1) {
            perror("Eroare la deschiderea bazei de date pentru verify!");
            exit(1);
        }

        struct header_db header;
        
        if (read(fd, &header, sizeof(struct header_db)) != sizeof(struct header_db)) {
            printf("Baza de date INVALIDA: Fisierul este prea mic sau corupt (nu s-a putut citi header-ul)!\n");
            close(fd);
            return 1;
        }
        if (strncmp(header.magic, "INV4", 4) != 0) {
            printf("Baza de date INVALIDA: Semnatura incorecta. Fisierul nu este o baza de date INV4!\n");
            close(fd);
            return 1;
        }
        if (header.complete == DB_PART_COMPLETE) {
            printf("Baza de date incompleta: Flag-ul 'complete' este 0!\n");

        }else if (header.complete == DB_FULL_COMPLETE)
        {
            printf("Baza de date completa: Flag-ul 'complete' este 1!\n");
        }
        
        printf("Baza de date este VALIDA!\n");
        printf("Versiunea %d, contine %d inregistrari de fisiere procesate de %d workeri.\n", header.version, header.file_record_count, header.worker_count);
        
        close(fd);
        return 0;
}
if (dump)
{
        int limit = 30;
        int fd = open(db_path, O_RDONLY);
        if (fd == -1) 
        {
            perror("Eroare la deschiderea bazei de date pentru dump!");
            exit(1);
        }
        struct header_db header;
        if (read(fd, &header, sizeof(struct header_db)) != sizeof(struct header_db)) 
        {
            printf("Eroare la citirea bazei de date!\n");
            close(fd); exit(1);
        }
        printf("magic = %s\n", header.magic);
        printf("version = %d\n", header.version);
        printf("complete = %d\n", header.complete);
        printf("file_record_count = %d\n", header.file_record_count);
        printf("worker_count = %d\n", header.worker_count);
        printf("FISIERELE INREGISTRATE ");


        if (header.file_record_count < limit)
        {
            limit = header.file_record_count;
        }
        
        for (int i = 0; i < limit; i++) // am pus o limita de 30 pentru ca atunci cand am indexat home nu mai vedeam partea de inceput cu afisarea headerului
        {
            struct FileRecord rec;
            if (read(fd, &rec, sizeof(struct FileRecord)) > 0) {
                printf("[%d] Path: %s\n", i+1, rec.path);
            }
        }

        int offset = sizeof(struct header_db) + (sizeof(struct FileRecord) * header.file_record_count);
        lseek(fd, offset, SEEK_SET);

        printf("PERFORMANTELE WORKERILOR\n");
        for (int i = 0; i < header.worker_count; i++) {
            struct WorkerStats stat;
            if (read(fd, &stat, sizeof(struct WorkerStats)) > 0)
            {
                printf("Worker %d: %d fisiere | Timp CPU: %ld us\n", stat.worker_id, stat.files_emitted, stat.user_cpu_us);
            }
        }

return 0;
}
 

if (strlen(root_dir) == 0 || nr_workers <= 0)
{
    printf("Eroare: Argumente insuficiente (--root sau --workers lipsesc/invalide)!\n");
    exit(1); 
}

    // 2. Verificam daca directorul root exista pe disc si este chiar un director
struct stat st;
if (stat(root_dir, &st) == -1 || !S_ISDIR(st.st_mode)) 
{
    printf("Eroare: Calea '%s' nu exista sau nu este un director!\n", root_dir);
    exit(2); 
}

if (access(root_dir, R_OK | X_OK) == -1) {
        printf("Eroare: Nu avem permisiuni de citire/executie pe directorul '%s'!\n", root_dir);
        exit(3);
    }

signal(SIGUSR1,  handler_sigusr1); 
signal(SIGINT,   handler_shutdown);
signal(SIGTERM,  handler_shutdown);
signal(SIGCHLD,  handler_sigchld); 

if (strlen(pid_file_path) > 0) {
    int fd = open(pid_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Eroare la crearea pid-file");
        exit(4);
    }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", getpid());
    write(fd, buf, len);
    close(fd);
}
   
//--------------------------------------------------------------------------

//-----------initializarea ipc-ului----------------

    int fd_ipc = open(ipc_path, O_RDWR | O_CREAT | O_TRUNC, 0666); 
    if (fd_ipc == -1) {
        perror("Eroare la crearea fisierului IPC");
        exit(1);
    }

    if (ftruncate(fd_ipc, sizeof(struct IPC_Memory)) == -1) 
    { //alocam memorie pe disc cat marimea structurii in care sunt structurile din header
        perror("Eroare la ftruncate");
        exit(1);
    }
    
    struct IPC_Memory *ipc = mmap(NULL, sizeof(struct IPC_Memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_ipc, 0); // e mult mai comod sa citesc si sa modific datele din fisierul mapat asa 
    if (ipc == MAP_FAILED) 
    {
        perror("Eroare la mmap");
        exit(1);
    }
    close(fd_ipc);

    memset(ipc, 0, sizeof(struct IPC_Memory)); //curat zona de memorie si dupa scriu datele preluate
    ipc->active_jobs = 1; 
    ipc->max_depth = max_depth;
    ipc->delay_ms = simulate_work_ms;

    sem_init(&ipc->global_mutex, 1, 1); //initializez semaforul global

    sem_init(&ipc->job_queue.mutex, 1, 1);        //initializez semafoarele pe coada 
    sem_init(&ipc->job_queue.items, 1, 0);         
    sem_init(&ipc->job_queue.spaces, 1, MAX_JOBS); 

    strncpy(ipc->job_queue.buffer[0].dir_path, root_dir, 4096); //adaug in coada directorul root
    ipc->job_queue.buffer[0].current_depth = 0;
    ipc->job_queue.head = 0;
    ipc->job_queue.tail = 1;
    
    sem_post(&ipc->job_queue.items);   //+1 item
    sem_wait(&ipc->job_queue.spaces);  //-1 spatiu
   
    for (int i = 0; i < nr_workers; i++) 
    { //setez semafoarele pentru fiecare canal al fiecarui worker
        sem_init(&ipc->result_channels[i].mutex, 1, 1);
        sem_init(&ipc->result_channels[i].items, 1, 0);
        sem_init(&ipc->result_channels[i].spaces, 1, MAX_RESULTS);
    }

    int control_pipe[2];
    if (pipe(control_pipe) == -1) {
        perror("Eroare la crearea pipe-ului de control"); //creez pipe-ul si setez capatul pentru citire al managerului pe modul neblocant
        exit(1);
    }
    
    int flags = fcntl(control_pipe[0], F_GETFL, 0);
    fcntl(control_pipe[0], F_SETFL, flags | O_NONBLOCK);

    pid_t worker_pids[MAX_WORKERS];

    for (int i = 0; i < nr_workers; i++)
    {
        pid_t pid = fork();

        if (pid == -1) 
        {
            perror("Eroare la fork");
            exit(1);
        }

        if (pid == 0)
        {
             char worker_id_str[10];
             char control_fd_str[10];
             sprintf(worker_id_str, "%d", i); //execl primeste doar string uri
             sprintf(control_fd_str, "%d", control_pipe[1]);

             execl("./bin/fileops_worker", "fileops_worker", "--worker-id", worker_id_str, "--ipc", ipc_path, "--control-fd", control_fd_str, NULL);
            
             perror("Eroare la execl!");
             exit(1);
        }
        else
        {
            worker_pids[i] = pid; //salvez pid ul acestui copil 
            printf("pid-ul parintelui: %d, pid-ul copilului: %d\n", getpid(), pid);
        }
        
    }

    close(control_pipe[1]); //inchid capatul de scriere pentru manager
    int alive_workers=nr_workers;
    int files_colected=0;
    int complete_flag  = DB_FULL_COMPLETE; //presupunem ca se termina normal, frumos, cum ar trebui si il schimbam dupa in caz de ceva

    printf("MANAGER: Acum adun informatiile de la workeri\n");

    const char* tmp_path = "data/records.tmp";
    int fd_tmp = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0666); // un fisier temporar in care scriu informatiile colectate de la workeri pe care le voi scrie dupa in baza de date
    if (fd_tmp == -1) {
        perror("Eroare la fisierul temporar");
        exit(1);
    }

    while (alive_workers)
    {
        //verificam intai flag urile

        if (flag_status) 
        {
            flag_status = 0;  
            print_status(ipc, nr_workers, worker_pids, files_colected, complete_flag);
        }
        if (flag_shutdown) 
        {
            flag_shutdown = 0;
            printf("MANAGER: Initializez shutdown gratios!\n");
            complete_flag = DB_PART_COMPLETE;  

            for (int i = 0; i < nr_workers; i++) //inainte de shutdown managerul colecteaza fisierele care se gasesc pe canalele workerilor
            {
                while (sem_trywait(&ipc->result_channels[i].items) == 0) {
                    struct FileRecord rec = ipc->result_channels[i].buffer[ipc->result_channels[i].head];
                    write(fd_tmp, &rec, sizeof(rec));
                    files_colected++;
                    ipc->result_channels[i].head = (ipc->result_channels[i].head + 1) % MAX_RESULTS;
                    sem_post(&ipc->result_channels[i].spaces);
                }
            }

            graceful_shutdown(worker_pids, nr_workers, graceful_timeout, ipc);
            alive_workers = 0; //dupa shutdown gratios nu mai avem workeri in viata
            break;
        }

        if (flag_sigchld) 
        {
            flag_sigchld = 0;
            for (int i = 0; i < nr_workers; i++) {
                if (worker_pids[i] != 0) {
                    int status;
                    pid_t p = waitpid(worker_pids[i], &status, WNOHANG);
                    if (p > 0) {
                        alive_workers--;
                        worker_pids[i] = 0;
                    }
                }
            }
        }

        //citesc mesajele din pipe si le afisez
        char buffer_pipe[4096];
        int bytes_cititi = read(control_pipe[0], buffer_pipe, sizeof(buffer_pipe) - 1);
        
        if (bytes_cititi > 0) {
            buffer_pipe[bytes_cititi] = '\0'; //adaug caracterul null pentru finalul stringului
            
            char *linie = strtok(buffer_pipe, "\n"); //pentru ca mesajele sunt lipite in buffer ele trebuie parsate
            while (linie != NULL) {        
                if (strncmp(linie, "T5MSG", 5) == 0) 
                {   
                    printf("[CONTROL PLANE] Am primit: %s\n", linie); //deocamdata parsez mesajul si il printez pentru ca nu prea stiu ce altceva ar trebui sa fac cu aceste mesaje  
                }
                linie = strtok(NULL, "\n");
            }
        }
        for (int i = 0; i < nr_workers; i++)
        {
            if (sem_trywait(&ipc->result_channels[i].items) == 0) //daca  mai avem fisiere in vectorul worker-ului curent reprezentate de semaforul items
            {
                struct FileRecord rec = ipc->result_channels[i].buffer[ipc->result_channels[i].head]; //partea de scriere in fisierul temporar
                write(fd_tmp, &rec, sizeof(struct FileRecord));
                files_colected++;
                ipc->result_channels[i].head = (ipc->result_channels[i].head + 1) % MAX_RESULTS; //mut indexul bufferului folosind circular ring buffer
                sem_post(&ipc->result_channels[i].spaces); //incrementam nr de spatii libere ale worker-ului care este evaluat
            }    
        }
        //verificam ce workeri mai sunt in viata
        int status;
        for (int i = 0; i < nr_workers; i++) 
        {
            if (worker_pids[i] != 0) 
            { 
                pid_t p = waitpid(worker_pids[i], &status, WNOHANG);
                if (p > 0) 
                {
                    alive_workers--;
                    worker_pids[i] = 0;
                }
            }
        } 
        
    }

    for (int i = 0; i < nr_workers; i++) {
        while (sem_trywait(&ipc->result_channels[i].items) == 0) {
            struct FileRecord rec = ipc->result_channels[i].buffer[ipc->result_channels[i].head];
            write(fd_tmp, &rec, sizeof(struct FileRecord));
            files_colected++;
            ipc->result_channels[i].head = (ipc->result_channels[i].head + 1) % MAX_RESULTS;
            // Nu mai e nevoie sa dam post la spaces pentru ca oricum mor
        }
    }

    printf("MANAGER: Inventariere %s. Am strâns %d fisiere.\n", complete_flag ? "finalizata" : "intrerupta (shutdown)", files_colected);
    write_database(db_path, ipc, nr_workers, files_colected, complete_flag, tmp_path);
        
    remove(tmp_path);
    close(control_pipe[0]);
    sem_destroy(&ipc->global_mutex);
    sem_destroy(&ipc->job_queue.mutex);
    sem_destroy(&ipc->job_queue.items);
    sem_destroy(&ipc->job_queue.spaces);
    for (int i = 0; i < nr_workers; i++) {
        sem_destroy(&ipc->result_channels[i].mutex);
        sem_destroy(&ipc->result_channels[i].items);
        sem_destroy(&ipc->result_channels[i].spaces);
    }
    if (munmap(ipc, sizeof(struct IPC_Memory)) == -1) {
        perror("Eroare la munmap");
    }
    if (unlink(ipc_path) == -1) {
        perror("Eroare la stergerea fisierului IPC");
    }
    if (strlen(pid_file_path) > 0) {
        remove(pid_file_path);
    }
   
    
    return 0;
}