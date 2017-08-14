#!/bin/bash



./petr > 0.pet &
./petr > 1.pet &
./petr > 2.pet &

for i in {1..100};
do 
	gcc -fopenmp -Wall -o petr$i TRparallelEdit.c 
	./petr$i > pet.$i &
done

