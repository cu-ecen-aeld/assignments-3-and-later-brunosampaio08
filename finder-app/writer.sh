#!/usr/bin/bash

writefile=$1
writestr=$2

if [ -z "$writefile" ]; then
	echo "No arguments supplied. Usage:"
	echo "$ writer.sh <writefile> <writestr>"
	exit 1
elif [ -z "$writestr" ]; then
	echo "Write string not supplied. Usage:"
	echo "$ writer.sh <writefile> <writestr>"
	exit 1
fi

# if file exists AND is not a directory, can't create dir
if [ -a "${writefile%/*}" -a ! -d "${writefile%/*}" ]; then
	echo "Cannot create dir because file exists."
	exit 1
else
	mkdir -p "${writefile%/*}"
fi

echo "$writestr" > "$writefile"
