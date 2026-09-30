#! /bin/bash
./tools/fileops.sh init

for directory in bin src include data logs reports tmp tests doc tools; do
    
    if [ ! -d $directory ]; then
        echo "Directorul $directory lipseste!"
        exit 5
    fi
done

exit 0
