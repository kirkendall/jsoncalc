# Start with empty versions of the generated files
for i in *.template
do
	sed '/@@@/,$d' $i >${i/.template/}
done

for i in *.html
do
	case "$i" in
	INTRO.html)	;;
	SKELETON.html)	;;
	*)
		# Extract info from the file
		description=$(sed -n 's/.*<meta name="description" content=".* - \([^"]*\) operators\?".*/\1/p' $i)
		sections=$(sed -n 's/.*<meta name="keywords" content="\(.*\), jsoncalc[^"]*".*/\1/p' $i | tr -d ,)
		htmlops=$(sed -n 's/.*<meta name="keywords" content=".*, operator reference, \([^"]*\)".*/\1/p' $i);
		operators=$(echo $htmlops| sed 's/,//g;s/\&lt;/</g;s/\&gt;/>/g;s/\&amp;/\&/g')


		# Generate the sidebar's list
		echo "<a id=\"${i/.html/}\" class=\"$sections operator\" href=\"../index.html?op=${i/.html}\" target=\"_PARENT\" title=\"$htmlops\">$description</a>" >>sidebar.html

		# Output info for each function name
		for op in $operators
		do
			# Generate a JavaScript index
			echo "{opcategory:\"$op\",src:\"$i\",query:\"${i/.html}\",description:\"$description\",sections:[\"${sections// /\",\"}\"]}," >>oplist.js
		done
		;;
	esac
done

# Finish off the generated files
for i in *.template
do
	sed '1,/@@@/d' $i >> ${i/.template/}
done
