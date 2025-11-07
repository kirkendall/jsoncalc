# Start with empty versions of the generated files
for i in *.template
do
	sed '/@@@/,$d' $i >${i/.template/}
done

currentdir=""
for i in *.html */*.html
do
	case "$i" in
	INTRO.html)	;;
	SKELETON.html)	;;
	sidebar.html)	;;
	*)
		# Extract the directory name from the filename
		case "$i" in
		*/*.html) dir="$(dirname $i)";;
		*)	dir="";;
		esac

		# Extract info from the file
		description=$(sed -n 's/.*<meta name="description" content=".* - \([^"]*\)".*/\1/p' $i)
		commands=$(sed -n 's/.*<meta name="keywords" content=".*, command reference, \([^"]*\)".*/\1/p' $i | tr -d ,)
		case "$commands" in
		expr) htmlcmds="<var>expr</var>";;
		"lvalue=expr") htmlcmds="<var>lvalue</var>=<var>expr</var>";;
		*) htmlcmds="$commands";;
		esac

		# For plugins' commands, use the plugin name as a section
		if [ "$dir" != "" ]
		then
			sections="$sections $dir"
		fi

		# If changing directories, then start a new grouping
		if [ "$dir" != "$currentdir" ]
		then
			if [ "$currentdir" != "" ]
			then
				echo "</fieldset>" >> sidebar.html
			fi
			echo "<fieldset><legend>plugin $dir</legend>" >>sidebar.html
			currentdir="$dir"
		fi

		# Output info for each command
		for c in $commands
		do
			# Generate a JavaScript index
			echo "{command:\"$c\",src:\"$i\",query:\"${i/.html}\",description:\"$description\"]}," >>cmdlist.js

			# Generate the INTRO file's list
			#echo "<tr class=\"$sections\"><td><a href=\"$i\">$f</a></td><td>$description</td></tr>" >>INTRO.html

		done

		# Generate the sidebar's list
		echo "<a id=\"${i/.html/}\" class=\"command\" href=\"../index.html?cmd=${i/.html}\" title=\"$description\" target=\"_PARENT\">$htmlcmds</a>" >>sidebar.html
		;;
	esac
done

# Finish off the generated files
if [ "$currentdir" != "" ]
then
	echo "</fieldset>" >>sidebar.html
fi
for i in *.template
do
	sed '1,/@@@/d' $i >> ${i/.template/}
done
