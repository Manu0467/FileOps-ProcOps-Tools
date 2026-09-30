#! /bin/bash

mkdir -p ./tmp/scenariu_test_src/app ./tmp/scenariu_test_src/lib/include
touch ./tmp/scenariu_test_src/app/main_demo.c
touch ./tmp/scenariu_test_src/lib/include/util.h
touch ./tmp/scenariu_test_src/lib/util.c

{
    echo "#pragma once"
    echo "int util_add(int a, int b);"
} > ./tmp/scenariu_test_src/lib/include/util.h

{ 
echo "#include <stdio.h>" 
echo "#include \"util.h\""
echo ""
echo "int util_add(int a, int b) { "
echo "return a+b; }"
} > ./tmp/scenariu_test_src/lib/util.c

{
    echo "#include <stdio.h>" 
    echo "#include \"util.h\""    
    echo ""
    echo "int main(){ "
    echo "printf(\"%d\", util_add(2,3));"
    echo ""
    echo "return 0; }"
} > ./tmp/scenariu_test_src/app/main_demo.c

    
 ./tools/fileops.sh build --src ./tmp/scenariu_test_src

if [ -x ./bin/demo ]; then
    ./bin/demo > ./tmp/demo_out.txt

    if [[ $? != 0 ]]; then
        echo "Eroare la rularea executabilului!"
        exit 7
    fi
    result=$(cat ./tmp/demo_out.txt )
else
    echo "Nu exista executabilul!"
    exit 8
fi

if [[ $result == "5" ]]; then
    echo "Rezultatul este corect!"

else
    echo "Nu stii matematica si nici programare pentru ca nu ai scris functia aia corect!!!!!!!!!!!!!!"
    exit 9
fi

exit 0