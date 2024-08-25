#!/bin/sh
#
# This script scans *.html files in the $dirs directories for keywords, and
# generates a kwlist.js list to allow searching in index.html
out="kwlist.js"
dirs="cmd f lit op"
stoplist="........*\|.\|..\|.*test\|ziggy.*\|yy*\|the\|.\|..\|you\|your\|yes\|yep\|no\|x*a*\|would\|wouldn\|works*\|words*\|with\|without\|within\|will\|won\|why\|who\|whose\|whole\|while\|which\|whether\|where\|when\|what\|were\|weren\|well\|ways*\|was\|wants*\|wanted\|void\|via\|very\|version\|versatile\|verbatim\|vcustomer\|vary\|variety\|values*\|valueappend\|valueassign\|val\|uxx*\|usual\|usually\|using\|uses\|use\|useful\|used\|upon\|unlike\|understands\|under\|unless\|udca\|too\|tools*\|together\|title\|tips\|thus\|though\|those\|things*\|thunk\|they\|these\|there\|therefore\|then\|them\|themselves\|their\|than\|test\|tested\|terday\|tells*\|technically\|syntax\|syntactically\|symbolwhat\|symbols\|symbol\|such\|str\|stportlandor\|storing\|still\|steve\|steven\|stephen\|src\|somewhat\|sometimes\|some\|single\|since\|simple\|simply\|simplest\|similarly\|should\|series\|separate\|separately\|separated\|selecting\|selectively\|selected\|see\|seen\|seemed\|same\|sake\|right\|returns\|returning\|results*\|really\|ready\|quirks*\|quirky\|question\|quick\|possible\|politely\|peter.*\|per\|perators\|perfectly\|pboy\|paul\|pass\|passed\|passing\|part.*\|pars.*\|parentheses\|parameters*\|pair.*\|own\|over\|overall\|out\|our\|otherwise\|other\|otable\|org\|orders\|optional.*\|operators*\|operands*\|only\|one\|once\|omit\|often\|omitted\|obj\|objects*\|num\|numbers*\|null\|not\|now\|nullfalse\|nothing\|notes*\|notation\|normal.*\|non\|none\|next\|new\|never\|need.*\|necessary\|nearly\|names*\|named\|must\|much\|most\|mostly\|more.*\|might\|method\|members*\|means*\|meaning\|mdash\|may\|maybe\|match.*\|mary\|makes*\|lot\|loop\|looping\|looks\|little\|listed\|likely\|lets*\|less\|left.*\|leaves*\|large\|larger\|largest\|keys*\|kbd\|keep.*\|just\|json\|jsoncalc\|javascript\|its\|itself\|items*\|isn\|into\|internal.*\|interchange.*\|integers*\|instead\|instances*\|instantly\|inside\|insensitively\|innertext\|indicates*\|indeed\|include.*\|how\|however\|honestly\|holds\|high\|higher\|highest\|here\|hence\|heavy\|having\|have\|has\|groups\|grouping\|functions*\|full\|from\|for\|form.*\|flag\|first\|firstname\|find\|file.*\|fields\|false\|fancy\|extra\|expressions*\|expris\|except\|after\|added\|adding\|all\|also\|and\|always\|any\|are\|aren\|arg\|arrays*\|arr\|but\|can\|case\|click\|copy\|data\|doesn\|don\|does\|each\|either\|else\|end\|every\|expr\|foo\|gets*\|given*\|got\|return\|that\|this"

# For each *.html file in $dirs
(
echo "var kwlist = ["
find $dirs -name '*.html' -print | grep -v '[A-Z]' | while read filename
do
	# Extract the title text
	title="$(sed -n 's/.*<title>\(.*\)<\/title>.*/\1/p' $filename)"
	if [ "$title" = "" ]
	then
		title=$(dirname $filename)
	fi

	# The query string is the *.html filename without the directory
	query=$(basename $filename .html)
	dir=$(dirname $filename)

	# For each keyword in the file...
	sed 's/<[^>]*>//g; s/&lt;/</g; s/&gt;/>/g; s/&quot;/"/g; s/&amp;/\&/g; s/[A-Za-z]\+/\n&\n/g' $filename | tr A-Z a-z | grep '[a-z]' | grep -v '/old/' |grep -v -x "$stoplist" | sort | uniq -d | while read word
	do
		# Generate a keyword index entry
		echo "{keyword:\"$word\",src:\"$filename\",query:\"$query\",dir:\"$dir\",description:\"$title\"},"
	done
done
echo "];"
) > "$out"
