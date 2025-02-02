#!/bin/sh

# Run scan.sh in any subdirectores first
for i in */scan.sh
do
	(cd $(dirname $i); sh scan.sh)
done

# INTRO.html from INTRO.html.template
sed '/@@@/,$d' INTRO.html.template >INTRO.html
cat a/SUMMARY.html >>INTRO.html
sed '1,/@@@/d' INTRO.html.template >>INTRO.html
