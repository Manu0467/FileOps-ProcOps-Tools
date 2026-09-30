#!/bin/bash

    mkdir -p ./scenariu_test/dir1/dir2/dir3
    touch ./scenariu_test/fisier1.txt ./scenariu_test/dir1/fisier2.txt ./scenariu_test/dir1/dir2/dir3/fisier3.txt

    echo "Rulez managerul pentru 5 workeri"
    ./tools/fileops.sh run -- fileops_manager --root ./scenariu_test --workers 5 --db ./data/test_db.db

    if [ "$?" != 0 ]; then
        echo "Eroare la rularea managerului!"
        exit 1
    fi

    if [ -s "./data/test_db.db" ]; then
        echo "Baza de date exista si are continut nevid!"
    else
        echo "Baza de date nu exista!"
        exit 2
    fi

    echo "Acum rulez --verify"
    ./tools/fileops.sh run -- fileops_manager --verify --db ./data/test_db.db

    if [ "$?" != 0 ]; then
        echo "Eroare la rularea --verify!"
        exit 3
    fi

    echo "Acum rulez --dump"
    ./tools/fileops.sh run -- fileops_manager --dump --db ./data/test_db.db > ./tests/rezultat_dump.txt

    grep "magic = INV4" ./tests/rezultat_dump.txt

    if [ "$?" != 0 ]; then
        echo "Eroare la rularea --dump!"
        exit 4
    fi

    grep "complete = 1" ./tests/rezultat_dump.txt

    if [ "$?" != 0 ]; then
        echo "Eroare la rularea --dump!"
        exit 5
    fi
    grep "file_record_count = 3" ./tests/rezultat_dump.txt

    if [ "$?" != 0 ]; then
        echo "Eroare la rularea --dump!"
        exit 6
    fi
    grep "worker_count = 5" ./tests/rezultat_dump.txt

    if [ "$?" != 0 ]; then
        echo "Eroare la rularea --dump"
        exit 7
    fi

    rm -rf ./scenariu_test ./data/test_db.db ./tests/rezultat_dump.txt





