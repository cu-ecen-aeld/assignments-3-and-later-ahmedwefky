#!/bin/sh

# Check arguments
if [ "$#" -lt 2 ]; then
  echo "Usage: $0 <directory> <search-string>"
  exit 1
fi

directory="$1"
string="$2"

# Check if directory exists
if [ ! -d "$directory" ]; then
  echo "Directory '$directory' does not exist."
  exit 1
fi

# Count files
number_files=$(find "$directory" -type f 2>/dev/null | wc -l)

# Count matching lines
number_matches=$(grep -R -I -F -- "$string" "$directory" 2>/dev/null | wc -l)

echo "The number of files are $number_files and the number of matching lines are $number_matches"
exit 0