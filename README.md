# FileOps & ProcOps Tools

A Linux systems programming project built entirely in C (C11) and Bash. This project implements a multiprocess file system inventory and process observer. It was built from scratch without external libraries to demonstrate a hands-on understanding of low-level operating system mechanisms.

## How it works under the hood

The system relies on a Manager-Worker architecture where the manager spawns multiple worker processes using `fork()` and `execl()`. To make everything work together safely and efficiently, the project implements:

* **Inter-Process Communication (IPC):** The system explicitly separates data and control flows.
  * *Data Plane:* Heavy data (like file metadata and SHA256 hashes) moves through a shared memory region (`mmap` with `MAP_SHARED`) using a circular ring buffer.
  * *Control Plane:* Short status messages and errors are sent from workers to the manager via non-blocking anonymous pipes.
* **Synchronization:** POSIX semaphores are used to manage the producer-consumer ring buffer and implement backpressure. For persistent storage, concurrent database updates are protected using `fcntl()` exclusive file locks.
* **Signal Handling & Graceful Shutdown:** The manager intercepts signals like `SIGINT` or `SIGTERM` to coordinate a safe shutdown. Instead of abruptly crashing, it stops accepting new jobs, waits for workers to finish, and flushes a valid (but partially completed) binary database to the disk. It also handles `SIGUSR1` to print real-time status updates.
* **Binary Databases & Snapshots:** Output is persisted in versioned, binary `.db` files with a strict header structure. A custom `db_diff` utility is included to compare different snapshots and track changes over time.

## Project Structure & Orchestration

The entire lifecycle of the project is managed by a unified Bash script (`tools/fileops.sh`) that acts as a custom build system and test runner.

### Quick Start

```bash
# 1. Initialize the strict directory layout
./tools/fileops.sh init

# 2. Incrementally compile all C source files
./tools/fileops.sh build

# 3. Run the manager with 4 workers to scan a directory
./tools/fileops.sh run fileops_manager --root /path/to/scan --workers 4

# 4. Run the automated bash test suites
./tools/fileops.sh test

# 5. Clean up build artifacts
./tools/fileops.sh clean
