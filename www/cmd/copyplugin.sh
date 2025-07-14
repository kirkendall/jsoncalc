#!/bin/sh

# For each plugin directory that documents functions...
for i in ../../src/plugin/*/www/cmd
do
	# Extract the plugin name from the directory
	plugin=${i/..\/..\/src\/plugin\//}
	plugin=${plugin/\/www\/cmd/}

	# Create the subdirectory for its functions.
	rm -rf "$plugin"
	mkdir "$plugin"

	# Copy all *.html files into it except SKELETON.html
	cp ../../src/plugin/"$plugin"/www/cmd/*.html "$plugin"/ 2>/dev/null
	rm -f "$plugin"/SKELETON.html
done
