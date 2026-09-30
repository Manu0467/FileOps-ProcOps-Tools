#! /bin/bash

mkdir -p data reports tests
rm -f data/*db reports/T3_filediff.txt reports/T3_procdiff.txt  # fac curatenie inainte de a incepe 

#partea de creare a filediff
    ./tools/fileops.sh run -- fileops_indexer --root reports/fs & ./tools/fileops.sh run -- fileops_indexer --root reports & ./tools/fileops.sh run -- fileops_indexer --root reports/process & ./tools/fileops.sh run -- fileops_indexer --root tools
    wait # astept sa se termine toate procesele astea 

index_1=$(ls -t data/index_*.db | head -n 1) # caut ultimul fisier index_db creat 
sleep 2 # ii dau sleep pentru ca daca nu ii dau o sa am o singura baza de date

 ./tools/fileops.sh run -- fileops_indexer --root reports/fs & ./tools/fileops.sh run -- fileops_indexer --root reports/proc & ./tools/fileops.sh run -- fileops_indexer --root include & ./tools/fileops.sh run -- fileops_indexer --root src
    wait
index_2=$(ls -t data/index_*.db | head -n 1)

if [ -s $index_1 ] && strings "$index_1" | grep -q "SEALED"  && strings "$index_1" | grep -q "IDX" ; then #verific daca fisierul are continut nevid
    echo "Primul fisier index $index_1 a fost creat corect, are SEALED si magic IDX !"
fi
if [ -s $index_2 ] && strings "$index_2" | grep -q "SEALED"  && strings "$index_2" | grep -q "IDX" ; then
    echo "Al doilea fisier index $index_2 a fost creat corect, are SEALED si magic IDX !"
fi

echo " "
echo "Se ruleaza db_diff pentru $index_1 si $index_2"
./tools/fileops.sh  run -- db_diff --old "$index_1" --new "$index_2"

echo "Generare snapshot-uri procese"

./tools/fileops.sh run -- proc_snapshot & ./tools/fileops.sh run -- proc_snapshot & ./tools/fileops.sh run -- proc_snapshot & ./tools/fileops.sh run -- proc_snapshot 
wait #asteptam sa se termine executia paralela

proc_1=$(ls -t data/proc_*.db | head -n 1)
sleep 2

./tools/fileops.sh run -- proc_snapshot & ./tools/fileops.sh run -- proc_snapshot & ./tools/fileops.sh run -- proc_snapshot & ./tools/fileops.sh run -- proc_snapshot 
wait 

proc_2=$(ls -t data/proc_*.db | head -n 1)


if [ -s "$proc_1" ] && strings "$proc_1" | grep -q "SEALED" && strings "$proc_1" | grep -q "PROC"; then
    echo "Fisierul $proc_1 a fost creat corect, este SEALED si are magic PROC!"
fi
if [ -s "$proc_2" ] && strings "$proc_2" | grep -q "SEALED" && strings "$proc_2" | grep -q "PROC"; then
    echo "Fisierul $proc_2 a fost creat corect, este SEALED si are magic PROC!"
fi

echo " "

echo "Se ruleaza db_diff pentru $proc_1 si $proc_2"
echo " "

./tools/fileops.sh run -- db_diff --old "$proc_1" --new "$proc_2"

if [ -s "./reports/T3_filediff.txt" ]; then
    echo "Raportul T3_filediff.txt exista si contine date."
else
    echo "Raportul T3_filediff.txt lipseste sau este gol!"
    exit 1
fi

if [ -s "./reports/T3_procdiff.txt" ]; then
    echo "Raportul T3_procdiff.txt exista si contine date."
else
    echo "Raportul T3_procdiff.txt lipseste sau este gol!"
    exit 1
fi

echo "Toate testele au fost rulate cu succes!"














 