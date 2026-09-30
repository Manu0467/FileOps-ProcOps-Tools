#!/bin/bash

timestamp_inceput=$(date "+%H:%M:%S")  
inceput=$(date "+%s")

function init {

        mkdir -p ./{bin,src,include,data,logs,reports,tmp,tests,doc,tools};

        if (gcc -v 2>&1 | grep "gcc version"); then
                echo "GCC exista, mai sus vezi si versiunea"
        else
                echo "GCC NU exista!! instaleaza si tu compilatorul"
                exit 1
        fi

}

function parcurgere_director(){ 

        local director="$1" fis cale obj_path
        for fis in  $(ls -A $1); do
                
                cale=$director/$fis
                obj=${fis%.c}.o
                obj_path="./tmp/obj/$obj"

                if [ -d "$cale" ]; then
                        parcurgere_director "$cale"

                elif [ -f "$cale" ]; then
                
                        if [[ "$fis" == *.c ]]; then

                                if [[ "$cale" -nt "$obj_path" || ! -f "$obj_path" ]]; then
                                               
                                        gcc $CFLAGS -c "$cale" -o "$obj_path"
                                fi 
                                if  grep -q "main" "$cale"; then #daca nu e main construim lista care e un string foarte lung nu un array
                                                mains="$mains $obj_path"
                                else
                                                libs="$libs $obj_path"
                                fi            
                                
                        fi

                fi
                        
        done
}
function build {

        mkdir -p ./tmp/obj
        echo "Bob the builder s-a apucat de treaba"
        SOURCE=./src

        if [[ $1 == "--src" ]]; then
                if [[ $# == 1 ]]; then
                        echo "Nu ai introdus sursa!"
                        exit 2
                else
                        SOURCE=$2        
                fi
        fi
        local dirs=$(find ./include $SOURCE -type f -name "*.h" -exec dirname {} \; | sort -u)
        CFLAGS=""
        for d in $dirs; do
                CFLAGS="$CFLAGS -I$d"
        done

        libs="" #libs trebuie sa fie globala declarata aici ptr ca dupa iar o sa am bucla infinita
        mains=""
        parcurgere_director "$SOURCE"
        
        for obj in $mains; do #aici e etapa de linking, mai parcurgem odata toate fisierele obiect de tip main si creez executabilele 
                nume=$(basename "$obj")
                temp=${nume#main_}
                exec=${temp%.o}
                gcc $CFLAGS $obj $libs -std=c11 -Wall -Wextra -o ./bin/$exec 
        done

}

function run {

        while [ "$#" -gt 0 ] && [ "$1" != "--" ];do
                shift #shiftam pana gasim --
        done

        shift #shiftam din nou pentru ca argumentul $1 sa fie primul numele executabilului
        local exe="./bin/$1"
        shift #acum shiftam ptr ca $1 sa fie primul argument al executabilului
        
        if [[ -x "$exe" ]]; then
                "$exe" "$@"
        else
                echo "Executabilul $exe nu a fost gasit! Verifica scriptul si vezi daca l-ai creat in primul rand!"
                exit 3
        fi




}

function clean {
        echo "Se curăță artefactele de build..."
        rm --force ./tmp/obj/*.o
        rm --force ./bin/*
        echo "Am terminat curatenia!"
}

function test {
        echo "Test results are: " > ./reports/T2_tests.txt 
        for file in $(find ./tests -type f -name "*.sh"); do    
                $file
                if [[ $? != 0 ]]; then
                        echo "test $file result: FAIL" >> ./reports/T2_tests.txt 
                        return 4
                fi
                echo "test $file result: PASS" >> ./reports/T2_tests.txt 
        done
}

subcomanda=""
while [ "$#" -gt 0 ]; do
        case "$1" in
                init) 
                        init; subcomanda="init";
                        shift
                        break;;

                build) 

                        shift; subcomanda="build";
                         if [[ $1 == "--src" ]]; then
                                build "$1" "$2"
                                shift 2
                         else
                                build 
                         fi 
                         break;;
                clean) 
                        clean; subcomanda="clean";
                        shift
                        break;;
                run) 
                        shift; subcomanda="run";
                        run "$@"
                        break;;

                test) 
                        test; subcomanda="test"
                        shift
                        break;;
                *) echo "Nu ati introdus o comanda valida!";
                exit 10;;
        esac
done

cod_exit=$?
timestamp_final=$(date "+%H:%M:%S")  
final=$(date "+%s")
timp_rulare=$((final - inceput))
anul=$(date +"%Y%m%d")
ora=$(date "+%H%M%S")

{
        echo "Subcomanda rulata: $subcomanda"
        echo "Timestamp inceput: $timestamp_inceput"
        echo "Timestamp final: $timestamp_final"
        echo "Timp de rulare: $timp_rulare"
        echo "Exit code: $cod_exit"

} > ./logs/fileops_"$anul"_"$ora".log