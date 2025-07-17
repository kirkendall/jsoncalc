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
	sidebar.html)	;;
	*)
		# Extract info from the file
		description=$(sed -n 's/.*<meta name="description" content=".* - \([^"]*\)".*/\1/p' $i)
		commands=$(sed -n 's/.*<meta name="keywords" content=".*, command reference, \([^"]*\)".*/\1/p' $i | tr -d ,)

		# Output info for each function name
		for c in $commands
		do
			# Generate a JavaScript index
			echo "{command:\"$c\",src:\"$i\",query:\"${i/.html}\",description:\"$description\"]}," >>cmdlist.js

			# Generate the INTRO file's list
			#echo "<tr class=\"$sections\"><td><a href=\"$i\">$f</a></td><td>$description</td></tr>" >>INTRO.html

			# Generate the sidebar's list
			echo "<a class=\"command\" href=\"../index.html?cmd=${i/.html}\" target=\"_PARENT\">$c<a>" >>sidebar.html
		done
		;;
	esac
done

# Finish off the generated files
for i in *.template
do
	sed '1,/@@@/d' $i >> ${i/.template/}
done
