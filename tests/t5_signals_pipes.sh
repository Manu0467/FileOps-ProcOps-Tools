#!/bin/bash

set -eo pipefail

MANAGER="./bin/fileops_manager"
PID_FILE="data/manager.pid"
DB_FILE="data/test_t5.db"
IPC_FILE="data/test_t5.mmap"
LOG_FILE="logs/test_t5_output.log"
TEST_DIR="data/t5_test_tree"

echo "Test tema 5"


rm -rf "$PID_FILE" "$DB_FILE" "$IPC_FILE" "$LOG_FILE" "$TEST_DIR"
mkdir -p "$TEST_DIR" logs data

for i in {1..10}; do 
    mkdir -p "$TEST_DIR/dir_$i"
    touch "$TEST_DIR/dir_$i/file_"{1..50}.txt
done


"$MANAGER" --root "$TEST_DIR" --workers 3 --ipc "$IPC_FILE" --db "$DB_FILE" --simulate-work-ms 10 --pid-file "$PID_FILE" > "$LOG_FILE" 2>&1 &

# Asteptam sa fie creat fisierul PID
for _ in {1..50}; do [ -f "$PID_FILE" ] && break; sleep 0.1; done

if [ -f "$PID_FILE" ]; then
    MGR_PID=$(cat "$PID_FILE")
    echo "[PASS] Manager pornit (PID=$MGR_PID)"
else
    echo "[FAIL] Fisierul PID nu a fost creat!"
    exit 1
fi

#  Test SIGUSR1 (Status)
kill -SIGUSR1 "$MGR_PID"
for _ in {1..30}; do grep -q "^STATUS " "$LOG_FILE" && break; sleep 0.1; done

if grep -q "^STATUS " "$LOG_FILE"; then
    echo "[PASS] A raspuns la SIGUSR1 (STATUS)"
else
    echo "[FAIL] Nu a raspuns la SIGUSR1"
    exit 1
fi

#  Test Pipe (Mesaje T5MSG)
for _ in {1..30}; do grep -q "T5MSG type=" "$LOG_FILE" && break; sleep 0.1; done

if grep -q "T5MSG type=" "$LOG_FILE"; then
    echo "[PASS] A procesat mesaje pe Pipe"
else
    echo "[FAIL] Nu a gasit mesaje pe Pipe"
    exit 1
fi

#  Test SIGTERM (Shutdown Gratios)
kill -SIGTERM "$MGR_PID"
for _ in {1..100}; do kill -0 "$MGR_PID" 2>/dev/null || break; sleep 0.1; done

if kill -0 "$MGR_PID" 2>/dev/null; then
    echo "[FAIL] Nu s-a oprit la SIGTERM. Fortez oprirea (SIGKILL)."
    kill -9 "$MGR_PID" 2>/dev/null || true
    exit 1
else
    echo "[PASS] S-a oprit gratios la SIGTERM"
fi

#  Test validare Baza de Date incompleta (complete = 0)
if [ ! -f "$DB_FILE" ]; then
    echo "[FAIL] Baza de date nu a fost generata inainte de oprire!"
    exit 1
fi

if "$MANAGER" --db "$DB_FILE" --dump 2>&1 | grep -q "complete = 0"; then
    echo "[PASS] Flag-ul bazei de date este complete = 0"
else
    echo "[FAIL] Flag-ul bazei de date NU este 0"
    exit 1
fi

if "$MANAGER" --db "$DB_FILE" --verify >/dev/null 2>&1; then
    echo "[PASS] Comanda --verify accepta corect DB-ul incomplet"
else
    echo "[FAIL] Comanda --verify a respins DB-ul"
    exit 1
fi

#  Test executie neintrerupta (complete = 1)
rm -f "$DB_FILE" "$IPC_FILE" "$PID_FILE"
"$MANAGER" --root "$TEST_DIR" --workers 2 --ipc "$IPC_FILE" --db "$DB_FILE" > /dev/null 2>&1

if "$MANAGER" --db "$DB_FILE" --dump 2>&1 | grep -q "complete = 1"; then
    echo "[PASS] O executie normala seteaza complete = 1"
else
    echo "[FAIL] Eroare la setarea complete = 1"
    exit 1
fi

rm -rf "$PID_FILE" "$DB_FILE" "$IPC_FILE" "$LOG_FILE" "$TEST_DIR"

echo "Toate testele au fost trecute cu succes!"