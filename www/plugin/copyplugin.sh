#!/bin/sh

for i in ../../src/plugin/*/.
do
	i=${i:0:-2}
	plugin=$(basename $i)
	echo "'$plugin'"

	# Copy the plugin's "index.html" as "$plugin.html"
	if [ -f "$i/www/index.html" ]
	then
		cp "$i/www/index.html" "$plugin.html"
	fi

	# NOTE: The function and command documentation is copied by
	# the copyplugin.sh scripts in ../f and ../cmd directories.
done
