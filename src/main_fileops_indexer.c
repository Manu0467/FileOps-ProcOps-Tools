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
#include <errno.h>




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
    char path[4096];
    char fileType[16];
    long size;
    long mtime;
    char binary_name[64];
    int32_t checksum;
    int32_t snapshot_id;
};

void parcurgere_director(int fd, char *path);
void write_in_database(int fd, struct Record* p);
int checksum(char *path);
void bin_name(char *name, int ino, int dev);
int check_entry(int fd, char* name);

int main(int argc, char* argv[]){


    int fd_db;
    char root_path[4098];
    char new_name[4098];

    if (argc<2)
    {
        perror("Nu a fost introdusa nicio cale!!");
    }

    strcpy(root_path, argv[2]);

    if (argc<=3)
    {
        fd_db=open("./data/index.db", O_RDWR);
        if (fd_db==-1)
        {
            fd_db = open("./data/index.db", O_RDWR | O_CREAT, 0644);
        }
        
    }else{
        fd_db=open(argv[4], O_RDWR);
        if (fd_db==-1)
        {
            fd_db=open(argv[4], O_RDWR | O_CREAT, 0644);

        }
        
    } //aici am facut verificarile pentru fisierul care trebuie deschis

    //in continuare e partea in care scriu in header 


    struct flock l;
    struct Header h;

    l.l_whence=SEEK_SET; //punctul de start de unde incep sa pun lacatul
    l.l_start=0; //offsetul
    l.l_len=sizeof(struct Header); // lungimea portiunii pe care pun lacat
    l.l_type=F_WRLCK;

    //verific daca sunt prima instanta care scrie in fisier prin verificarea marimii fisierului si acum realizez ca ar trebui sa fac asta sub lacat
    fcntl(fd_db, F_SETLKW, &l); //am pus un lacat blocant adica am folosit F_SETLKW nu F_SETLK
    long size=lseek(fd_db, 0, SEEK_END); // este long ptr ca m-am gandit ca fisierul poate sa fie destul de mare

    if (size==0)
    {
        memset(&h, 0, sizeof(struct Header)); //curatam headerul de valorile junk de la initializare
        strncpy(h.magic, "IDX", sizeof(h.magic));
        h.format_version=1;
        h.snapshot_id=time(NULL);
        strncpy(h.snapshot_state, "OPEN", sizeof(h.snapshot_state) - 1);
        h.active_writers=1;
        h.record_count=0;

        lseek(fd_db, 0, SEEK_SET); 
        write(fd_db, &h, sizeof(struct Header));

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
        printf("DEBUG: Active writers imediat dupa incrementare: %d\n", h.active_writers);
        lseek(fd_db, 0, SEEK_SET);
        write(fd_db, &h, sizeof(struct Header));

        l.l_type = F_UNLCK; 
        fcntl(fd_db, F_SETLK, &l);
    }


    parcurgere_director(fd_db, root_path); //parcurg directorul si scriu in database

    l.l_type=F_WRLCK; //pun lock pe header si dupa il actualizez
    fcntl(fd_db, F_SETLKW, &l);
    lseek(fd_db, 0, SEEK_SET);

    read(fd_db, &h, sizeof(struct Header));
    h.active_writers--;

    lseek(fd_db, 0, SEEK_SET);
    write(fd_db, &h, sizeof(struct Header));

    printf("DEBUG: Active writers dupa decrementare: %d\n", h.active_writers);

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

        snprintf(new_name, sizeof(new_name), "./data/index_%d.db" , h.snapshot_id);
        rename("./data/index.db", new_name);
        //printf("%s\n" , new_name);
    }else{

        l.l_type=F_UNLCK;
        fcntl(fd_db, F_SETLK, &l);
        close(fd_db);

    }

    

    return 0;
}

void bin_name(char *nume, int ino, int dev){

        char buffer[2048];
        sprintf(buffer, "%d", dev);
        strcpy(nume, buffer);
        buffer[0]='\0';
        sprintf(buffer, "%d", ino);
        strcat(nume, "_");
        strcat(nume, buffer);
        
}

int checksum(char *path){
    int fd=open(path, O_RDONLY);
    char ch;
    int sum=0;

    do
    {
        int cd_er=read(fd, &ch, 1);
        if (cd_er==-1)
        {
            printf("Eroare la deschiderea fisierului in functia checksum");
            return -1;
        }
        if (cd_er==0)
        {
            break;
        }
        sum+=(int)ch;
          
    } while (1);
    
    close(fd);
    return sum;
}

int check_entry(int fd, char* name){

    struct Record rec;
    lseek(fd, sizeof(struct Header), SEEK_SET);
    while (read(fd, &rec, sizeof(struct Record)) != 0)
    {
        if (strcmp(rec.binary_name, name) == 0)
        {
            return lseek(fd, 0, SEEK_CUR) - sizeof(struct Record); //returnam pozitia de la care incepe intrarea
        }
        
    }
    return -1;
    
}

void write_in_database(int fd, struct Record* p){

   int offset = check_entry(fd, p->path);

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
        
        offset = check_entry(fd, p->path); 
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

void parcurgere_director(int fd, char *path){

    struct stat s;
    struct Record rec;
    lstat(path, &s);
    strncpy(rec.path, path, sizeof(rec.path));
    rec.mtime=s.st_mtime;
    rec.size=s.st_size;
    bin_name(rec.binary_name, s.st_ino, s.st_dev);
  
    if (lstat(path, &s) == -1)
    {
    perror("lstat error");
    return;

    }

    if (S_ISDIR(s.st_mode))
    {
     strncpy(rec.fileType, "DIRECTORY", sizeof(rec.fileType));
     rec.size=0; 
    }
    else if (S_ISREG(s.st_mode))
    {
        strncpy(rec.fileType, "REGULAR_FILE", sizeof(rec.fileType));
        rec.size=s.st_size;
        rec.checksum=checksum(path);
    }
    else if (S_ISFIFO(s.st_mode))
    {
        strncpy(rec.fileType, "FIFO", sizeof(rec.fileType));
        rec.size=0;
    }
    else if (S_ISLNK(s.st_mode))
    {
        strncpy(rec.fileType, "SYM_LINK", sizeof(rec.fileType));
        rec.size=0;
    }

    //printf("DEBUG: Scanez acum: %s\n", path);
    write_in_database(fd, &rec);

    if (S_ISDIR(s.st_mode))
    {
   
     DIR *dd; //directory descriptor 
     struct dirent *de;

     if ((dd=opendir(path))==NULL)
     {
        printf("Eroare la deschiderea fisierului in functia parcuregere_director");
     }
     while ((de=readdir(dd)) != NULL)
     {
        if( strcmp(de->d_name,".") && strcmp(de->d_name,"..") ){
            char next_path[4096];
            snprintf(next_path, sizeof(next_path), "%s/%s", path, de->d_name);
            parcurgere_director(fd,next_path);

        }
     }
     closedir(dd);
     
    }

}