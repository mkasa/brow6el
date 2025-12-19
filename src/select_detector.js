// JavaScript to be injected into pages to detect and extract select element info

(function() {
    console.log('[Brow6el] Select detector initializing...');
    
    // Prevent double initialization
    if (window._brow6el_initialized) {
        console.log('[Brow6el] Already initialized, skipping');
        return;
    }
    window._brow6el_initialized = true;
    
    // Store currently focused select element
    window._brow6el_focused_select = null;
    window._brow6el_select_options = [];
    window._brow6el_select_index = -1;
    
    function handleFocus(e) {
        console.log('[Brow6el] Focus event on:', e.target.tagName, 'id:', e.target.id, 'class:', e.target.className);
        if (e.target.tagName === 'SELECT') {
            console.log('[Brow6el] SELECT focused!');
            window._brow6el_focused_select = e.target;
            
            // Extract options
            var options = [];
            for (var i = 0; i < e.target.options.length; i++) {
                options.push(e.target.options[i].text);
            }
            window._brow6el_select_options = options;
            window._brow6el_select_index = e.target.selectedIndex;
            
            // Notify C++ side
            console.log('BROW6EL_SELECT_FOCUSED:' + JSON.stringify({
                options: options,
                selectedIndex: e.target.selectedIndex
            }));
        }
    }
    
    function handleBlur(e) {
        if (e.target.tagName === 'SELECT') {
            console.log('[Brow6el] SELECT blurred');
            window._brow6el_focused_select = null;
            console.log('BROW6EL_SELECT_BLURRED');
        }
    }
    
    // Listen for focus events on select elements (capture phase)
    document.addEventListener('focus', handleFocus, true);
    document.addEventListener('blur', handleBlur, true);
    
    // Also add click listener to detect clicks on selects and trigger focus
    document.addEventListener('click', function(e) {
        console.log('[Brow6el] Click on:', e.target.tagName, 'id:', e.target.id);
        if (e.target.tagName === 'SELECT') {
            console.log('[Brow6el] SELECT clicked! Manually triggering focus...');
            // Manually trigger focus since CEF doesn't seem to do it automatically
            e.target.focus();
            // Manually call handleFocus since the focus event might not fire
            handleFocus({target: e.target, type: 'focus'});
        }
    }, true);
    
    // Add mousedown listener too
    document.addEventListener('mousedown', function(e) {
        if (e.target.tagName === 'SELECT') {
            console.log('[Brow6el] SELECT mousedown!');
        }
    }, true);
    
    console.log('[Brow6el] Event listeners attached');
    
    // Function C++ can call to get current select info
    window._brow6el_get_select_info = function() {
        if (window._brow6el_focused_select) {
            return {
                options: window._brow6el_select_options,
                selectedIndex: window._brow6el_select_index
            };
        }
        return null;
    };
    
    console.log('[Brow6el] Select detector ready');
})();
