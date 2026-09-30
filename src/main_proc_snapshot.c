#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <linux/stat.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <ctype.h> //ptr isdigit

#define MAX_CMD 256

struct Header
{
    char magic[8];
    int32_t format_version;
    int32_t snapshot_id;
    char snapshot_state[8];
    int32_t active_writers;
    int32_t record_count;
};

struct Record
{
    int32_t PID;
    int32_t PPID;
    long CPU_time;
    char state;
    char comm[256];
    char cmdline[256];
    int32_t RSS;
    int32_t virtual_mem;

};

void write_in_database(int fd, struct Record* p);
void parcurgere_proc(int fd_db);
int  is_pid(char * path);
int  get_data(char* pid, struct Record *rec);
int check_entry(int fd, struct Record* v);

int main(int argc, char* argv[]){

    int fd_db;
    char new_name[4098];

    if (argc>1)
    {
        fd_db=open(argv[2], O_RDWR);
        if (fd_db==-1)
        {
            fd_db=open(argv[2], O_RDWR | O_CREAT , 0644);
        }
        
    }else{

        fd_db=open("./data/proc.db", O_RDWR);
        if (fd_db==-1)
        {
            fd_db=open("./data/proc.db", O_RDWR | O_CREAT , 0644);
        }
    }//verificarile pentru deschiderea fisierelor binare

    //Setarea headerului
    struct flock l;
    struct Header h;

    l.l_whence=SEEK_SET;
    l.l_start=0;
    l.l_len=sizeof(struct Header);
    l.l_type=F_WRLCK;

    fcntl(fd_db, F_SETLKW, &l); 
    long size=lseek(fd_db, 0, SEEK_END);
    
    if (size==0)
    {

        memset(&h, 0, sizeof(struct Header)); //curatam headerul de valorile junk de la initializare
        strncpy(h.magic, "PROC", sizeof(h.magic));
        h.format_version=1;
        h.snapshot_id=time(NULL);
        strncpy(h.snapshot_state, "OPEN", sizeof(h.snapshot_state) - 1);
        h.active_writers=1;
        h.record_count=0;


        if (write(fd_db, &h, sizeof(struct Header)) == -1)
        {
            printf("Eroare la scrierea in baza de date(segmentation)");
        }
        

        l.l_type = F_UNLCK; 
        fcntl(fd_db, F_SETLK, &l);

    }else{
    
        lseek(fd_db, 0, SEEK_SET); //pozitionam cursorul la inceputul fisierului ptr ca e posibil sa fi scris ceva inainte si sa nu scriu bine acum daca nu fac asta
        read(fd_db, &h, sizeof(struct Header));
        if (strcmp(h.snapshot_state, "SEALED") == 0)
        {
            perror("Nu se mai poate scrie in fisier! snapshot_state = SEALED");
            l.l_type = F_UNLCK; 
            fcntl(fd_db, F_SETLK, &l);
            close(fd_db);
            return 2;
        }

        h.active_writers++;
        lseek(fd_db, 0, SEEK_SET);
        write(fd_db, &h, sizeof(struct Header));

        l.l_type = F_UNLCK; 
        fcntl(fd_db, F_SETLK, &l);   

        printf("Active-writers dupa incrementare: %d\n", h.active_writers);

    }

    parcurgere_proc(fd_db);


    l.l_type=F_WRLCK; //pun lock pe header si dupa il actualizez
    fcntl(fd_db, F_SETLKW, &l);
    lseek(fd_db, 0, SEEK_SET);

    read(fd_db, &h, sizeof(struct Header));
    h.active_writers--;
    printf("Active writers dupa  decrementare: %d\n", h.active_writers);

    lseek(fd_db, 0, SEEK_SET);
    write(fd_db, &h, sizeof(struct Header));

     if (h.active_writers==0)
    {
        memset(h.snapshot_state, 0, sizeof(h.snapshot_state));
        strncpy(h.snapshot_state, "SEALED", sizeof(h.snapshot_state) - 1);
        long long total_size=lseek(fd_db, 0, SEEK_END);
        total_size-=sizeof(struct Header);
        int record_count=total_size/sizeof(struct Record);
        h.record_count=record_count;

        lseek(fd_db, 0, SEEK_SET);
        write(fd_db, &h, sizeof(struct Header));


        l.l_type=F_UNLCK;
        fcntl(fd_db, F_SETLK, &l);
        close(fd_db);

        snprintf(new_name, sizeof(new_name), "./data/proc_%d.db" , h.snapshot_id);
        rename("./data/proc.db", new_name);
   
    }else{

        l.l_type=F_UNLCK;
        fcntl(fd_db, F_SETLK, &l);
        close(fd_db);

    }

    return 0;
}


int check_entry(int fd, struct Record* v){

    struct Record rec;
    lseek(fd, sizeof(struct Header), SEEK_SET);
    while (read(fd, &rec, sizeof(struct Record)) != 0)
    {
        if (v->PID == rec.PID)
        {
            return lseek(fd, 0, SEEK_CUR) - sizeof(struct Record); //returnam pozitia de la care incepe intrarea
        }
        
        
    }
    return -1;
    
}

int is_pid(char* name){

    for (int i = 0; name[i] != '\0'; i++)
    {
        if (!isdigit(name[i]))
        {
            return 0;
        }
        
    }
    return 1;
    
}

int get_data(char* pid, struct Record *rec){

        char lstat_path[2060];
        char cmdline_path[2060];
        snprintf(lstat_path, sizeof(lstat_path), "/proc/%s/stat", pid);
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", pid);

        FILE *fd_p=fopen(lstat_path, "r");
        if (fd_p == NULL)
        {
            return -1;
        }
        long utime, stime;
        int page_number;
        unsigned int page_size = sysconf(_SC_PAGESIZE);

        int fields = fscanf(fd_p, "%d (%255[^)]) %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld %*d %*d %*d %*d %*d %*d %*llu %*d %d",
           &rec->PID,
           rec->comm,
           &rec->state,
           &rec->PPID,
           &utime,
           &stime,
           &page_number);

        fclose(fd_p);

        rec->CPU_time = (utime + stime);
        rec->RSS = page_number * page_size;

        int fd_c=open(cmdline_path, O_RDONLY);
         //fisierul cmdline contine stringuri separate de \0
        int n = read(fd_c, rec->cmdline, MAX_CMD-1);

        if (fd_c != -1) {
        if (n > 0)
        {
            rec->cmdline[n] = '\0';

            for (int k = 0; k < n; k++)
            {
                if (rec->cmdline[k] == '\0' && k < n - 1) {
                    rec->cmdline[k] = ' '; 
                }
            }

        }else
        {
            strcpy(rec->cmdline, "no-content");
        }
        close(fd_c);}
        else
        {
            strcpy(rec->cmdline, "no-content");
        }
        
        return 0;   
}

void write_in_database(int fd, struct Record* p){

   int offset = check_entry(fd, p);

   if (offset != -1 )
   {
        struct flock lock;
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = offset;                     
        lock.l_len = sizeof(struct Record);   
        
        if (fcntl(fd, F_SETLKW, &lock) != -1) {
            lseek(fd, offset, SEEK_SET);
            write(fd, p, sizeof(struct Record));
            
            lock.l_type = F_UNLCK;
            fcntl(fd, F_SETLK, &lock);
        }
   }
   else
   {
        struct flock lock_header;
        lock_header.l_type = F_WRLCK;
        lock_header.l_whence = SEEK_SET;
        lock_header.l_start = 0;
        lock_header.l_len = sizeof(struct Header);

        fcntl(fd, F_SETLKW, &lock_header); 
        
        offset = check_entry(fd, p); 
        if (offset != -1 )
        {
           
            lock_header.l_type = F_UNLCK;
            fcntl(fd, F_SETLK, &lock_header);

            struct flock lck;
            lck.l_type = F_WRLCK;
            lck.l_whence = SEEK_SET;
            lck.l_start = offset;                     
            lck.l_len = sizeof(struct Record);   
            
            if (fcntl(fd, F_SETLKW, &lck) != -1) {
                lseek(fd, offset, SEEK_SET);
                write(fd, p, sizeof(struct Record));
                
                lck.l_type = F_UNLCK; 
                fcntl(fd, F_SETLK, &lck);
            }
        }
        else
        {
            lseek(fd, 0, SEEK_END);
            write(fd, p, sizeof(struct Record));  
            
            lock_header.l_type = F_UNLCK;
            fcntl(fd, F_SETLK, &lock_header);
        }
   }
}

void parcurgere_proc(int fd_db){

    struct Record rec;
    struct stat st;
    char path[2048]; //folosesc stat pentru ca nu pot sa verific cu DT_DIR pentru ca nu imi returneaza nimic asa ca fac cu S_ISDIR 

    DIR *dd; //directory descriptor 
    struct dirent *de;

     if ((dd=opendir("/proc"))==NULL)
     {
        printf("Eroare la deschiderea fisierului /proc in functia parcuregere_proc");
     }
     
     while ((de=readdir(dd)) != NULL)
     {
        if( strcmp(de->d_name,".") && strcmp(de->d_name,"..") )
        {
          
            snprintf(path, sizeof(path), "/proc/%s", de->d_name);
            lstat(path, &st);

             if (S_ISDIR(st.st_mode) && is_pid(de->d_name))
            {
    
                memset(&rec, 0, sizeof(struct Record)); // Curatăm junk-ul
                if (get_data(de->d_name, &rec) == 0) {
                    
                    write_in_database(fd_db, &rec);

                }
                
            }
        }
     }
     closedir(dd);
     
    

}
