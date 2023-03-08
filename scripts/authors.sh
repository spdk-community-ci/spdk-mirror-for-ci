#!/bin/bash
TMPFILE=`mktemp`
TMPFILE_OG=`mktemp`
TMPFILE_NEW=`mktemp`
git log $1..$2 | grep "^Author" | awk '{print $NF}' > $TMPFILE

total_commit_count=`cat $TMPFILE | wc | awk '{print $1}'`
total_committer_count=`cat $TMPFILE | sort | uniq -c | wc | awk '{print $1}'`
cat $TMPFILE | sort | uniq -c > ./list.tmp

# Ziye sometimes posts patches using optimistyzy@gmail.com address.
external_commit_count=`grep -v "@intel.com" $TMPFILE | grep -v "optimistyzy" | wc | awk '{print $1}'`
external_committer_count=`grep -v "@intel.com" $TMPFILE | grep -v "optimistyzy" | sort | uniq -c | wc | awk '{print $1}'`

lines_changed=`git diff --shortstat $1..$2 | awk '{ print $4+$6; }'`

echo "Total commit count: $total_commit_count"
echo "Total committer count: $total_committer_count"
echo "External commit count: $external_commit_count"
echo "External committer count: $external_committer_count"
echo "Total lines changed (additions+deletions): $lines_changed"

#cat $TMPFILE | sort | uniq -c | sort -n
#rm -f $TMPFILE

git log $1 | grep "^Author" | awk '{printf "%s %s %s\n", $(NF-2), $(NF-1), $(NF)}' | awk -F" " '!_[$3]++' | sort | uniq > $TMPFILE_OG
git log $1..$2 | grep "^Author" | awk '{printf "%s %s %s\n", $(NF-2), $(NF-1), $(NF)}' | awk -F" " '!_[$3]++' | sort | uniq > $TMPFILE_NEW
#cat $TMPFILE_NEW
comm -13 <(cat $TMPFILE_OG) <(cat $TMPFILE_NEW)
