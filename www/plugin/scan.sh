#!/bin/sh

# Build the INTRO.html and sidebar.html files
sed '/@@@/,$d' INTRO.html.template >INTRO.html
sed '/@@@/,$d' sidebar.html.template >sidebar.html
for i in *.html
do
	# Skip if its generate from a template
	if [ -f $i.template ]
	then
		continue
	fi

	# Extract the plugin name from the filename
	plugin=${i/.html/}

	# Extract the title from the file
	title="$(sed -n 's/.*<title>\(.*\)<\/title>.*/\1/p' $i)"

	# Add it to the INTRO.html
	(
		echo "<tr>"
		echo "<td><a target=\"_PARENT\" href=\"../index.html?p=$plugin\">$plugin</a></td>"
		echo "<td>$title</td>"
		echo "</tr>"
	) >>INTRO.html

	# Add it to the sidebar.html
	(
		echo "<a target=\"_PARENT\" class=\"plugin\" href=\"../index.html?p=$plugin\" title=\"$title\">$plugin</a>"
	) >>sidebar.html
done
sed '1,/@@@/d' INTRO.html.template >>INTRO.html
sed '1,/@@@/d' sidebar.html.template >>sidebar.html



