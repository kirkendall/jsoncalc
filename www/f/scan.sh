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
		sections=$(sed -n 's/.*<meta name="keywords" content="\(.*\), jx[^"]*".*/\1/p' $i | tr -d ,)
		functions=$(sed -n 's/.*<meta name="keywords" content=".*, function reference, \([^"]*\)".*/\1/p' $i | tr -d ,)

		# For plugins' functions, use the plugin name as a section
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

		# Output info for each function name
		for f in $functions
		do
			# Generate a JavaScript index
			echo "{function:\"$f\",src:\"$i\",query:\"${i/.html}\",description:\"$description\",sections:[\"${sections// /\",\"}\"]}," >>flist.js

			# Generate the INTRO file's list
			echo "<tr class=\"$sections\"><td>$dir</td><td><a target=\"_PARENT\" href=\"../index.html?f=${i/.html/}\">$f</a></td><td>$description</td></tr>" >>INTRO.html

			# Generate the sidebar's list
			echo "<a id=\"$f\" class=\"$sections function\" href=\"../index.html?f=${i/.html}\" target=\"_PARENT\">$f()</a>" >>sidebar.html
		done
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
