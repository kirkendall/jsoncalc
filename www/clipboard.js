// A simple function to copy a string to clipboard.
// The wacky function-returning-a-function notation is just a way to add a
// static variable where the function can store the clipped text.
// See https://github.com/lgarron/clipboard.js for a full solution.
// and https://stackoverflow.com/questions/34045777/copy-to-clipboard-using-javascript-in-ios
var copyToClipboard = (function() {
    // Create a hidden <textarea>.  This happens once, during intialization.
    var textarea = document.createElement("TEXTAREA");
    textarea.style.opacity = 0; // Invisible, but capable of getting focus
    textarea.style.height = 0;  // Don't expand page
    textarea.style.position = "fixed"; // Don't cause scrolling
    textarea.style.top = 0;   // Always at top of scroll view
    textarea.style.left = 0;  // Always at left of scroll view
    textarea.contentEditable = true;
    textarea.readOnly = false;
    window.setTimeout(function(){
        // Delay adding it to body until there is a body
        document.body.appendChild(textarea);
    }, 1000);

    // Return a function that does the actual copying.  This is what's called
    // whenever you say copyToClipboard("some text").
    return function(data) {
        // Set input focus on the textarea.  Can't select without focus.
        textarea.focus();

        // Stuff the data into the textarea
        textarea.value = data;

        // Visually select the contents of the textarea.  This is analogous
        // to doing a draw-through using the mouse.
        var range = document.createRange();
        range.selectNodeContents(textarea);
        var selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        textarea.setSelectionRange(0, data.length);

        // Initiate a "copy"
        document.execCommand("copy");
    };
})();

