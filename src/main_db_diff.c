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

struct Record_proc
{
    int32_t PID;
    int32_t PPID;
    long CPU_time;
    char state;
    char comm[256];
    char cmdline[16];
    int32_t RSS;
    int32_t virtual_mem;

};

struct Record_index
{
    char path[4096];
    char fileType[16];
    long size;
    long mtime;
    char binary_name[64];
    int32_t checksum;
    int32_t snapshot_id;
};

void compare_proc(int fd_old, int fd_new, char* output_path);
void compare_index(int fd_old, int fd_new, char* output_path);

int main(int argc, char* argv[]){

    char old_db[100], new_db[100], output_db[100];


   switch (argc)
   {
   case 1: perror("Nu ai introdus niciun fisier pentru comparare!"); return 1;

   case 3: perror("Ai introdus un singur fisier! Nu are cu cine sa fie comparat!"); return 2;

   case 5: 
    strcpy(old_db, argv[2]);
    strcpy(new_db, argv[4]);
    break;

    case 7:
    strcpy(old_db, argv[2]);
    strcpy(new_db, argv[4]);
    strcpy(output_db, argv[6]);
    break;
   }

   int fd_old, fd_new, fd_out;
   struct Header h_old, h_new;

   fd_old=open(old_db, O_RDONLY);
   fd_new=open(new_db, O_RDONLY);

   if (fd_new == -1 || fd_old == -1)
   {
        perror("Eroare la deschiderea unuia dintre fisiere");
        return 1;
   }

   lseek(fd_new, 0, SEEK_SET);
   lseek(fd_old, 0, SEEK_SET);
   read(fd_new, &h_new, sizeof(struct Header));
   read(fd_old, &h_old, sizeof(struct Header));

   if (strcmp(h_new.magic, h_old.magic) != 0)
   {
        perror("Fisierele introduse nu au acelasi magic!");
        return 3;
   }

   if (h_new.format_version != h_old.format_version)
   {
        perror("Bazele de date folosesc versiuni de format diferite!");
        close(fd_new);
        close(fd_old);
        return 4; 
   }

   if (strcmp(h_new.snapshot_state, "OPEN") == 0 || strcmp(h_old.snapshot_state, "OPEN") == 0  )
   {
     perror("Bazele de date nu pot fi comparate cat timp snapshot_state = OPEN!");
     return 5;
   }

   if (argc < 6) // aici verific daca calea ptr output a fost introdusa ca sa stiu daca deschid fisierele implicite
   {
        if (strcmp(h_new.magic, "IDX") == 0) //putea fi si h_old chiar nu conteaza din moment ce am facut verificarile de mai sus
        {
            strcpy(output_db, "./reports/T3_filediff.txt");
            fd_out = open(output_db, O_RDWR);
            if (fd_out == -1)
            {
                fd_out = open(output_db, O_RDWR | O_CREAT | O_TRUNC, 0644);
                close(fd_out);
            }
            
        }
        else if (strcmp(h_new.magic, "PROC") == 0)
        {
            strcpy(output_db, "./reports/T3_procdiff.txt");
            fd_out = open(output_db, O_RDWR);
            if (fd_out == -1)
            {
                fd_out = open(output_db, O_RDWR | O_CREAT | O_TRUNC, 0644);
                close(fd_out);
            }
            
        }
        
   } else{ // daca a fost introdusa calea verific aici daca exista fisierul

    fd_out=open(output_db, O_RDWR);
            if (fd_out == -1)
            {
                fd_out = open(output_db, O_RDWR | O_CREAT, 0644);
                close(fd_out);
            }

   }

   if (strcmp(h_new.magic, "IDX") == 0)
   {
        compare_index(fd_new, fd_old, output_db);
   }
   else if (strcmp(h_new.magic, "PROC") == 0){
    compare_proc(fd_new, fd_old, output_db);
   }
   
   
   
    close(fd_new);
    close(fd_old);
    return 0;
}

void compare_proc(int fd_new, int fd_old, char * output_path){

    FILE *output = fopen(output_path, "w");
    if (output == NULL)
    {
        perror("Eroare la deschiderea fisierului de output in compare_proc");
        return;
    }
    
    struct Record_proc old, new; 
    struct Header h_old, h_new;
    lseek(fd_new, 0, SEEK_SET);
    lseek(fd_old, 0, SEEK_SET);
    read(fd_new, &h_new, sizeof(struct Header));
    read(fd_old, &h_old, sizeof(struct Header));
    int cnt_ap = 0 , cnt_dsp = 0, cnt_mod = 0;
    lseek(fd_new, sizeof(struct Header), SEEK_SET);

    for (int i = 0; i < h_new.record_count; i++)
    {
        int found=0;
        read(fd_new, &new, sizeof(struct Record_proc));
        lseek(fd_old, sizeof(struct Header), SEEK_SET);
        
        for (int j = 0; j < h_old.record_count; j++)
        {
            read(fd_old, &old, sizeof(struct Record_proc));
            if (new.PID == old.PID)
            {
                found=1;

                int32_t rss_diff = new.RSS - old.RSS;
                long cpu_diff = new.CPU_time - old.CPU_time;

                if (rss_diff > 4096 || cpu_diff > 0 || old.state != new.state)
                {
                    fprintf(output, "PROCESUL %s A FOST MODIFICAT \n", new.comm);
                    cnt_mod++;
                }
                 break;
                
            }
            
        }

        if (found == 0)
        {
            fprintf(output, "PROCESUL %s A APARUT \n", new.comm);
            cnt_ap++;
        }
        
    }

      lseek(fd_new, sizeof(struct Header), SEEK_SET); //repozitionam cursorul in fiecare dintre fisiere
      lseek(fd_old, sizeof(struct Header), SEEK_SET);


    for (int i = 0; i < h_old.record_count; i++)
    {
        int found=0;
        read(fd_old, &old, sizeof(struct Record_proc));
        lseek(fd_new, sizeof(struct Header), SEEK_SET);

        for (int j = 0; j < h_new.record_count; j++)
        {
            read(fd_new, &new, sizeof(struct Record_proc));
            if (old.PID == new.PID)
            {
                found=1;
                break;
            }
            
        }

        if (found == 0)
        {
            fprintf(output, "PROCESUL %s A DISPARUT \n", old.comm);
            cnt_dsp++;
        }
        
        
    }

    fprintf(output, "Procese aparute: %d \n Procese disparute: %d\n Procese modificate: %d", cnt_ap, cnt_dsp, cnt_mod);
    fclose(output);

}

void compare_index(int fd_new, int fd_old, char* output_path){

    FILE *output = fopen(output_path, "w");
    if (output == NULL)
    {
        perror("Nu pot deschide fisierul de output in compare_index");
        return;
    }
    
    struct Record_index old, new; 
    struct Header h_old, h_new;
    lseek(fd_new, 0, SEEK_SET); //repozitionam cursorul in fiecare dintre fisiere
    lseek(fd_old, 0, SEEK_SET);
    read(fd_new, &h_new, sizeof(struct Header));
    read(fd_old, &h_old, sizeof(struct Header));
    int cnt_ap = 0 , cnt_dsp = 0, cnt_mod = 0;

    for (int i = 0; i < h_new.record_count; i++)
    {
        int found = 0;
        read(fd_new, &new, sizeof(struct Record_index));
        lseek(fd_old, sizeof(struct Header), SEEK_SET);
        for (int j = 0; j < h_old.record_count; j++)
        {
         read(fd_old, &old, sizeof(struct Record_index));   

         if (strcmp(new.path, old.path) == 0)
         {
            found = 1;
            if (strcmp(new.fileType, old.fileType) != 0 ||  new.size != old.size || new.mtime != old.mtime || new.checksum != old.checksum)
            {
                fprintf(output, "FISIERUL %s A FOST MODIFICAT \n", new.path);
                    cnt_mod++;
            }
            break;
            
         }
         
        }

        if (found == 0)
        {
            fprintf(output, "FISIERUL %s A APARUT \n", new.path);
            cnt_ap++;
        }
        
    }

    lseek(fd_new, sizeof(struct Header), SEEK_SET); //repozitionam cursorul in fiecare dintre fisiere
    lseek(fd_old, sizeof(struct Header), SEEK_SET);



    for (int i = 0; i < h_old.record_count; i++)
    {
        int found = 0;
        read(fd_old, &old, sizeof(struct Record_index));
        lseek(fd_new, sizeof(struct Header), SEEK_SET);

        for (int j = 0; j < h_new.record_count; j++)
        {
            read(fd_new, &new, sizeof(struct Record_index));
            if (strcmp(old.path, new.path) == 0)
            {
                found = 1;
            }
            
        }

        if (found == 0)
        {
            fprintf(output, "FISIERUL %s A DISPARUT \n", old.path);
            cnt_dsp++;
        }
    }
    
    fprintf(output, "Fisiere aparute: %d \n Fisiere disparute: %d\n Fisiere modificate: %d", cnt_ap, cnt_dsp, cnt_mod);
    fclose(output);
}

