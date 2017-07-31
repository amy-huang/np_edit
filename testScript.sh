#!/bin/bash

./petr > 0.pet &
./petr > 1.pet &
./petr > 2.pet &

for i in {0..100}
do
	./petr > pet.$i
& done
