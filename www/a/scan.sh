#!/bin/sh

# Start clean
rm INTRO.html
rm sidebar.html

# Extract the timestamp from each article, so we can sort them.
for i in *.html
do
    if [ $i != SKELETON.html ]
    then
	sed -n "s/ *<h4 class=\"timestamp\">\([^<]*\)<.*/\1\t$i/p" $i
    fi
done >> articles$$

# Build the "SUMMARY.html" file, including summaries of the 3 newest articles
sed '/@@@/,$d' SUMMARY.html.template >SUMMARY.html
sort -r articles$$ | head -3 | while read timestamp filename
do
    # Start it
    echo '<article>'
    a=${filename/.html}
    a=${a/ /+/g}
    echo "<a class=\"article\" target=\"_PARENT\" href=\"index.html?a=${filename/.html}\">"

    # Extract the title from the article
    sed -n 's/ *<title>\(.*\)<\/title>/<h3>\1/p' $filename

    # Output the timestamp
    echo "<span class=\"timestamp\">$timestamp</span>"

    echo "</h3>";

    # Extract the abstract from the article
    sed '1,/<blockquote class="abstract">/d;/<\/blockquote>/,$d' $filename

    # Finish it
    echo "</a>"
    echo "</article>"
done >>SUMMARY.html
sed '1,/@@@/d' SUMMARY.html.template >>SUMMARY.html

# Build the "INTRO.html" file, including summaries of all articles
sed '/@@@/,$d' INTRO.html.template >INTRO.html
sort -r articles$$ | while read timestamp filename
do
    # Start it
    echo '<article>'
    a=${filename/.html}
    a=${a/ /+/g}
    echo "<a class=\"article\" target=\"_PARENT\" href=\"../index.html?a=${filename/.html}\">"

    # Output the timestamp
    echo "<span class=\"timestamp\">$timestamp</span>"

    # Extract the title from the article
    sed -n 's/ *<title>\(.*\)<\/title>/<h3>\1<\/h3>/p' $filename

    # Extract the abstract from the article
    sed '1,/<blockquote class="abstract">/d;/<\/blockquote>/,$d' $filename

    # Finish it
    echo "</a>"
    echo "</article>"
done >>INTRO.html
sed '1,/@@@/d' INTRO.html.template >>INTRO.html

# Build the "sidebar.html" file, including titles of all articles
sed '/@@@/,$d' sidebar.html.template >sidebar.html
sort -k2 articles$$ | while read timestamp filename
do
    echo -n "<a target=\"_PARENT\" href=\"../index.html?a=${filename/.html/}\">"

    # Extract the title from the article
    sed -n 's/ *<h1>\(.*\)<\/h1>/\1<\/a>/p' $filename
done >>sidebar.html
sed '1,/@@@/d' sidebar.html.template >>sidebar.html

# Clean up
rm articles$$
