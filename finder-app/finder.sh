#!/usr/bin/bash

filesdir=$1
searchstr=$2

if [ -z "$filesdir" ]; then
	echo "No arguments supplied. Usage:"
	echo "$ finder.sh <seachdir> <searchstr>"
	return 1
elif [ -z "$searchstr" ]; then
	echo "Search string not supplied. Usage:"
	echo "$ finder.sh <seachdir> <searchstr>"
	return 1
elif [ ! -d "$filesdir" ]; then
	echo "Supplied filesdir is not a directory."
	return 1
fi

word_count=0
file_count=0

function search_dir(){
	for file in "$1"/*; do
		if [ -d "$file" ]; then
			search_dir "$file"
		else
			file_count=$((file_count+1))
			word_count=$(($(grep --count "$searchstr" "$file")+word_count))
		fi
	done
}

search_dir "$filesdir"

echo "The number of files are ""$file_count"" and the number of matching lines are ""$word_count"
