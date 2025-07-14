#!/bin/sh

# For each plugin directory that documents functions...
for i in ../../src/plugin/*/www/f
do
	# Extract the plugin name from the directory
	plugin=${i/..\/..\/src\/plugin\//}
	plugin=${plugin/\/www\/f/}

	# Create the subdirectory for its functions.
	rm -rf "$plugin"
	mkdir "$plugin"

	# Copy all *.html files into it except SKELETON.html
	cp ../../src/plugin/"$plugin"/www/f/*.html "$plugin"/ 2>/dev/null
	rm -f "$plugin"/SKELETON.html
done
