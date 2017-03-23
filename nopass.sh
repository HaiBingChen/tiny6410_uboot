#!/bin/sh

dir=$(find ${PWD} -name "*.o")
for d in $dir
do
	rm $d -rf
done   
