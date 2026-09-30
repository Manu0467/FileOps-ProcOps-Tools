# Manager-Worker Communication

## 1. IPC Format (Shared Memory)
Communication between the manager and workers is achieved by mapping a file into memory, defined by the `IPC_Memory` structure. This shared area contains:
* **global state variables:** `active_jobs` (which monitors how many directories are left to scan), `max_depth` (self-explanatory), and `delay_ms`.
* **job_que:** where the directories to be scanned are stored; it also acts as a ring buffer from which workers extract jobs. `job_que` is a structure of type `JobQue`.
* **result_channels:** an array containing elements of type `struct Result_channel`, which similarly uses the ring buffer concept to transmit the information collected by each worker. In turn, `struct Result_channel` contains 3 semaphores: one to keep track of free spaces, one to keep track of the elements currently in the queue, and a mutex semaphore.
* **worker_stats:** an array with elements of type `WorkerStats` where workers save various data about their performance, such as the number of indexed files, CPU time (`wall_time_ms`), PID, or PPID.

## 2. Synchronization
To prevent workers from writing over each other (race condition), synchronization is handled using anonymous POSIX semaphores (`sem_init`):
* **global_mutex:** a binary semaphore (acting as a lock, initialized with a value of 1) that protects access to global counters (e.g., `active_jobs`).
* **producer-consumer model:** for both `JobQue` and `ResultChannel`, 3 semaphores are used:
  * mutex: a semaphore acting as a lock to ensure processes do not overwrite each other when modifying the head and tail.
  * items: a semaphore indicating the number of elements in the buffer.
  * spaces: a semaphore indicating the number of empty spaces left in the buffer.

The backpressure mechanism is implemented via the `spaces` and `items` semaphores. If a worker has found too many files and its `ResultChannel` is full (the `MAX_RESULTS` limit has been reached), the `sem_wait(&spaces)` call will block it. Thus, the worker will wait until the manager consumes files from its channel and calls `sem_post(&spaces)`, thereby freeing up memory.

## 4. Chosen Limits
The limits were chosen so as not to overload the laptop too much while running this program:
* **MAX_WORKERS = 32**: a reasonable limit for indexing all files on the working laptop.
* **MAX_JOBS = 2048**: a sufficiently large queue to store the discovered files.
* **MAX_RESULTS = 100**: a relatively small buffer per channel to ensure efficient data transfer from the worker to the manager.
