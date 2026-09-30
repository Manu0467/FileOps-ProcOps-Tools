
```
## **1.** Common header for the proc_snapshot and fileops_indexer files
```c
struct Header
{
    char magic[8];            
    int32_t format_version;
    int32_t snapshot_id;
    char snapshot_state[8];
    int32_t active_writers;
    int32_t record_count;
};

```

Even though I could have used 6 bytes for "SEALED", I chose to stick with 8 because it provides better memory alignment.
`format_version` is the version, which I currently set to 1 when initializing the header, because I do not plan to change how the C source code works anytime soon.

I used `int32_t` to ensure my header will have the same size across any operating system and to prevent potential read errors that might have occurred if I used a standard "int" data structure, as there are cases where the size of an int can vary from laptop to laptop.

## **2.** Records structure

#### **2.1** Structure for file databases

```c
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

```

The `max_path` I chose is 4096 because this is the path limit on Linux operating systems, thus ensuring I can read any file without its path getting truncated.

The longest file type name is "REGULAR_FILE" which has 12 characters (excluding the null character); by making the size equal to a power of 2, I ensure no padding zones are added by the system, leading to better overall memory alignment.

The size of the binary name would be around 40 characters in the worst-case scenario, but here too I used the same memory allocation principle, namely allocating the closest power of 2 greater than or equal to the maximum number of characters the name has, to avoid that padding.

The reason for using `int32_t` is the same as for the header structure.

#### **2.2** Structure for process databases

```c
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

```

The `/proc/[pid]/cmdline` file contains the entire command including its arguments, which in various cases can be quite numerous; therefore, I chose a reasonable limit of 256, which is a power of 2 large enough to include the whole command and potentially the first arguments.
The `state` variable is a `char` type because in `/proc/[pid]/stat` the state of a process is represented by a single character (R, Z, S, T, etc.).
The `comm` variable stores the short name of the process; in Linux, the size of this variable in `stat` is 15 characters (+ the null character).

## **3.** Update strategy

The update strategy uses POSIX locks to lock sections of the file to allow multiple processes to update the data simultaneously. This is based on the following rules:

1. Lock on header (for metadata): when an instance modifies the file metadata located in the header, this is done under a lock so that data is not overwritten by another process.
2. Lock for update: if the element (the path or the PID) already exists, I place a fixed lock exactly on the area where the record is located, covering the length of that record, and I overwrite the data there using the offset found via a separate function. This ensures that multiple instances can perform updates simultaneously on different areas of the file without doing uncontrolled appends.
3. Lock for append: when I want to add a new file, I place a temporary lock on the header to ensure no other instance comes along and adds the same process I am trying to add, which would result in two identical entries. After locking the header, I check if the process was added in the meantime by another instance, and if not, I append it at the end and release the header.

## **4.** Criteria for a database to be considered valid

For a database to be considered valid and processable by the `db_diff` utility, it must pass a few conditions, which include:

1. `Snapshot_state` must be "SEALED". If this were "OPEN", it would mean instances are still writing to the database, it's undergoing continuous modifications, and the data is incomplete and unstable. A stable database is one that has been "sealed" with `snapshot_state="SEALED"`.
2. Valid signature (magic). The `PROC` or `IDX` identifier guarantees that the inputted databases are not random files, but exactly the databases that can be compared by `db_diff`.
3. Format version. If two files have different formats, comparing them would be useless since they do not follow the same rules (it's possible that 2 different versions have different sizes for the structures in `index` or `proc`, which could lead to read errors).

## **5.** Comparison rules in db_diff

The comparison rules in `db_diff` rely on finding the elements using a unique key (`path` for index and `pid` for proc) and checking the changes that occurred between the 2 databases.

1. For the `index.db` database, the record is searched by its path, and if any difference is observed in `file_type`, `size`, or the date of last modification (`mtime`), that file is considered modified; if not, it checks if that path exists in the other database and vice versa.
2. For the `proc.db` database, the record is searched by PID, which is unique for each process. A process is considered modified if its state changed (the `state` variable), if it registered new CPU consumption, or if more than one additional page of RAM was allocated.

```

```
