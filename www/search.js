function searchInput(jqElement)
{
    // The input element(s)
    var elem;

    // Return a version of text where <>&" are replaced with entities
    function htmlsafe(text)
    {
	return text.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;");
    }

    // Respond to a click in the list of possible matches
    function chosen(event)
    {
	location.replace("index.html?"+$(this).data("query"));
    }

    // Stuff any matching searchables into the popup, and maybe show the pop-up if
    // anything.  "near" is a DOM object to show the pop-up near, or undefined to
    // hide the pop-up.  Returns null unless there's only one match, in which case
    // it returns the parameter needed to take us to that item.
    function adjust(text, near)
    {
	// If less than 3 letters or 1 punctuation, no meaningful results are possible.
	var popup = $("div.popup");
	if (text.trim().length < 3) {
	    popup.css("display","none");
	    return;
	}

	// If supposed to hide, then hide
	if (!near)
	    popup.css("display","none");

	// Start empty
	popup.empty();
	var nfound = 0;

	// Convert search text to lowercase, to aide case-insensitive searches.
	text = text.toLowerCase();

	// For each searchable...
	for (var s = 0; s < searchable.length; s++) {
	    var first = true;
	    var list = searchable[s].list;
	    var member = searchable[s].member;
	    for (i = 0; i < list.length; i++) {
		if (list[i][member].toLowerCase().indexOf(text) >= 0
		 || list[i].description.toLowerCase().indexOf(text) >= 0) {
		    if (first) {
			first = false;
			popup.append("<div class=\"searchSection\">"+searchable[s].label+"</div>");
		    }
		    var html = "<div class=\"searchItem\"";
		    html += "data-query=\""+searchable[s].query+"="+encodeURIComponent(list[i].query)+"\">";
		    html += "<tt>"+htmlsafe(list[i][member])+"</tt> &nbsp; "
		    html += htmlsafe(list[i].description)+"</div>";
		    popup.append(html);
		    nfound++;
		}
	    }
	}

	// If nothing found then hide the pop-up
	if (nfound == 0) {
	    popup.css("display","none");
	    return null;
	}

	// Make all of the found divs be touch-sensitive
	popup.find("div.searchItem").on("click", chosen);

	// Show the pop-up, maybe.  Try to place it under the search input
	if (near) {
	    // Make visible, sort of.  We need to make it visible so it has a size,
	    // but we aren't ready for people to see it yet.
	    popup.css({display:"block",opacity:0,width:""});

	    // If the popup wants to be narrower than the search input, force it wider
	    var input = $(near);
	    if (popup.width() < input.width())
		popup.css({width: input.width()+"px"});

	    // Choose a position, based on the position of the search input
	    var position = input.offset();
	    position.top += input.height() + 5;
	    position.left += input.width() - popup.width() + 5;

	    // Move it and make it really visible
	    popup.css({opacity:1, top: position.top+"px", left: position.left+"px"});
	}

	// If exactly one item was found, return it's query
	if (nfound == 1)
	    return popup.find("div.searchItem").data("query");
	return null;
    }

    // Respond to keystrokes in the Search input.
    function incremental(event)
    {
	adjust($(this).val(), this);
    }

    // Respond to a final changed value in the Search input
    function change(event)
    {
	// Wait 0.1 seconds before doing anything.  This way if the user clicked on
	// an item in the pop-up, the pop-up will still be there long enough for
	// the browser to respond to the click.
	setTimeout(function(){
	    var query = adjust($(this).val());
	    if (query)
		location.replace("index.html?"+query);
	    adjust("");
	}, 100);
    }

    // Initialization
    elem = jqElement;
    $(elem).on("input", incremental)
	   .on("change", change);
}
