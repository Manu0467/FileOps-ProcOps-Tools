# Assignment 5 Documentation: Control Plane, Signals, and Graceful Shutdown

## 1. General Architecture Description

The system uses a **Manager** process that coordinates multiple **Worker** processes created via `fork()` and replaced via `execl()`.

* **Data Plane:** Implemented via shared memory (`mmap` on the `ipc.mmap` file) and synchronized using POSIX semaphores. It is used exclusively for transmitting large data structures (directory paths, `FileRecord`s containing metadata, and the SHA256 hash).
* **Control Plane:** Implemented via an **anonymous pipe** (Worker -> Manager unidirectional communication). It is used strictly for transmitting text status messages, formatted according to the `T5MSG` standard.


## 2. Signal Handling
To avoid concurrency issues, the handlers only set global variables of type `volatile sig_atomic_t`, with the main logic being safely executed within the programs' loops.

### Signals in Manager
* `SIGUSR1`: The Manager displays real-time system status information on the screen (queued jobs, active jobs, collected files, memory, alive workers). The output is immediately flushed to the screen using `fflush(stdout)`.
* `SIGINT` / `SIGTERM`: Triggers the **Graceful Shutdown** process. The Manager does not die instantly, but initiates the coordinated shutdown of the workers.
* `SIGCHLD`: Notifies the manager immediately when a child process finishes execution, preventing zombie processes via a `waitpid` with the `WNOHANG` flag.

### Signals in Worker
* `SIGTERM`: The worker exits the new directory wait loop, finishes processing the current directory, and exits cleanly, without blocking semaphores or corrupting the IPC.
* `SIGPIPE`: Ignored (`SIG_IGN`). If the manager stops abruptly and closes the read end of the pipe, the worker will not be killed by the system on the first `write`, but can handle the error locally.


## 3. Graceful Shutdown Mechanism
When the Manager receives a stop command (`SIGTERM` or `SIGINT`), it calls the `graceful_shutdown()` function, which follows a strict sequence to prevent data loss:

1.  **Stopping the workflow:** Sets `active_jobs = 0` in the IPC, forbidding workers from picking up or adding new directories to the queue.
2.  **Notification:** The Manager sends the `SIGTERM` signal to each active Worker.
3.  **Grace period (Timeout):** The Manager waits for the workers to stop, periodically checking (non-blocking polling with `usleep` and `waitpid`) if they have finished execution. The maximum wait time is given by the `--graceful-timeout` argument.
4.  **Forced Execution:** If a Worker gets stuck (e.g., reading a corrupted file) and exceeds the timeout, the Manager sends `SIGKILL` to clean up resources.
5.  **Database Generation:** The database is saved by completing the entire inventory collected up to that point, but the header is explicitly marked with `complete = 0` (`DB_PART_COMPLETE`).


## 4. Pipe Communication (Atomicity)
Workers communicate errors, progress, and final status to the Manager by sending strings over the anonymous pipe (the descriptor is received via the `--control-fd` argument).

* **Message Format:** Messages follow the strict format: `T5MSG type=<TYPE> <key>=<value>... \n`.
* **Write Atomicity:** To prevent mixing messages from multiple workers, the message is first formatted into a local buffer using `snprintf`. Because the message size is validated to be smaller than the system's `PIPE_BUF` constant, the call to the `write()` function guarantees that the message will enter the pipe atomically.

The Manager reads these messages by setting the pipe end to `O_NONBLOCK` mode and uses `strtok` to split the data by the `\n` character, thus retrieving multiple messages arriving simultaneously in the read buffer.


## 5. Preventing High CPU Load (Busy Waiting)
To prevent the Manager from using a CPU core at 100% capacity during the main loop (`while (alive_workers)`), an activity monitoring mechanism was implemented.
If the Manager completes a full loop iteration where it finds no new signals, no pipe messages, and no elements extracted from memory (mmap), it pauses with `usleep(1000)`. This micro-delay protects the host system's performance without affecting the program's responsiveness.


## 6. Automated Testing Script (Deterministic)
The implementation validation is done by a bash script (`t5_signals_pipes.sh`) based exclusively on **deterministic polling** instead of fixed pauses (`sleep 5`).
* The script ensures fail-fast (stops on the first error).
* `.pid` files are used for precise signal targeting.
* Validates the presence of the status in the generated output.
* Checks for correct shutdown on SIGTERM, including the internal state of the generated SQLite/binary file (`complete = 0` vs `complete = 1`) using the Manager's CLI tools (`--verify` and `--dump`).
