// Inspect mode - show element information on hover
(function() {
    // Skip if in a frameset without content
    if (window.frames.length > 0 && document.body && document.body.children.length === 0) {
        console.log('[Brow6el] INSPECT_MODE_FRAMESET - Inspect mode not available in framesets');
        return;
    }
    
    // Check if mouse emulation is active
    if (!window.__brow6el_mouse_emu) {
        console.log('[Brow6el] INSPECT_MODE_ERROR - Mouse emulation must be active');
        return;
    }
    
    const mouseEmu = window.__brow6el_mouse_emu;
    
    // Create or toggle inspect mode
    if (mouseEmu.inspectMode) {
        // Disable inspect mode
        mouseEmu.inspectMode = false;
        if (mouseEmu.inspectInfoBox) {
            mouseEmu.inspectInfoBox.remove();
            mouseEmu.inspectInfoBox = null;
        }
        if (mouseEmu.inspectHighlight) {
            mouseEmu.inspectHighlight.remove();
            mouseEmu.inspectHighlight = null;
        }
        mouseEmu.lastInspectedElement = null;
        console.log('[Brow6el] INSPECT_MODE:OFF');
        return;
    }
    
    // Enable inspect mode
    mouseEmu.inspectMode = true;
    mouseEmu.lastInspectedElement = null;
    
    // Create info box
    mouseEmu.inspectInfoBox = document.createElement('div');
    mouseEmu.inspectInfoBox.style.cssText = `
        position: fixed !important;
        background: rgba(0, 0, 0, 0.9) !important;
        color: #fff !important;
        padding: 10px !important;
        border: 2px solid #0ff !important;
        border-radius: 5px !important;
        font-family: monospace !important;
        font-size: 12px !important;
        z-index: 2147483646 !important;
        pointer-events: none !important;
        max-width: 400px !important;
        word-wrap: break-word !important;
        display: none !important;
        line-height: 1.4 !important;
    `;
    
    // Create highlight box
    mouseEmu.inspectHighlight = document.createElement('div');
    mouseEmu.inspectHighlight.style.cssText = `
        position: fixed !important;
        border: 2px solid #0ff !important;
        background: rgba(0, 255, 255, 0.1) !important;
        z-index: 2147483645 !important;
        pointer-events: none !important;
        display: none !important;
    `;
    
    const target = document.body || document.documentElement;
    if (!target) {
        console.log('[Brow6el] INSPECT_MODE_ERROR - No document body or element available');
        return;
    }
    target.appendChild(mouseEmu.inspectInfoBox);
    target.appendChild(mouseEmu.inspectHighlight);
    
    // Override updatePosition to also update inspect info
    const originalUpdatePosition = mouseEmu.updatePosition;
    mouseEmu.updatePosition = function() {
        originalUpdatePosition.call(this);
        
        if (!this.inspectMode) return;
        
        // Get element at cursor position
        if (this.cursor) this.cursor.style.display = 'none';
        if (this.inspectInfoBox) this.inspectInfoBox.style.display = 'none';
        if (this.inspectHighlight) this.inspectHighlight.style.display = 'none';
        
        const el = document.elementFromPoint(this.x, this.y);
        
        if (this.cursor) this.cursor.style.display = '';
        
        if (!el || el === this.lastInspectedElement) {
            if (el) {
                // Still on same element, keep showing info
                if (this.inspectInfoBox) this.inspectInfoBox.style.display = '';
                if (this.inspectHighlight) this.inspectHighlight.style.display = '';
            }
            return;
        }
        
        this.lastInspectedElement = el;
        
        // Get element information
        const rect = el.getBoundingClientRect();
        const computedStyle = window.getComputedStyle(el);
        
        // Build info text
        let info = '<div style="color: #0ff; font-weight: bold; margin-bottom: 5px;">';
        info += el.tagName.toLowerCase();
        if (el.id) info += '#' + el.id;
        if (el.className && typeof el.className === 'string') {
            const classes = el.className.split(' ').filter(c => c.trim());
            if (classes.length > 0) info += '.' + classes.join('.');
        }
        info += '</div>';
        
        // Add attributes
        const attrs = [];
        if (el.type) attrs.push(['type', el.type]);
        if (el.name) attrs.push(['name', el.name]);
        if (el.href) attrs.push(['href', el.href]);
        if (el.src) attrs.push(['src', el.src]);
        if (el.value !== undefined && el.value !== '') attrs.push(['value', el.value]);
        if (el.placeholder) attrs.push(['placeholder', el.placeholder]);
        if (el.title) attrs.push(['title', el.title]);
        if (el.alt) attrs.push(['alt', el.alt]);
        
        if (attrs.length > 0) {
            info += '<div style="margin: 5px 0; font-size: 11px;">';
            for (const [key, val] of attrs) {
                const truncated = val.length > 50 ? val.substring(0, 47) + '...' : val;
                info += '<div><span style="color: #ff0;">' + key + ':</span> ' + 
                        truncated.replace(/</g, '&lt;').replace(/>/g, '&gt;') + '</div>';
            }
            info += '</div>';
        }
        
        // Add dimensions
        info += '<div style="margin-top: 5px; font-size: 11px; color: #aaa;">';
        info += Math.round(rect.width) + 'x' + Math.round(rect.height) + 'px';
        info += ' @ (' + Math.round(rect.left) + ',' + Math.round(rect.top) + ')';
        info += '</div>';
        
        // Add text content (if any, truncated)
        const textContent = el.textContent?.trim() || '';
        if (textContent && textContent.length > 0) {
            const truncated = textContent.length > 80 ? textContent.substring(0, 77) + '...' : textContent;
            info += '<div style="margin-top: 5px; font-size: 11px; color: #8f8;">"' + 
                    truncated.replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/\n/g, ' ') + '"</div>';
        }
        
        this.inspectInfoBox.innerHTML = info;
        
        // Position info box (avoid edges)
        let infoX = this.x + 20;
        let infoY = this.y + 20;
        
        // Adjust if too close to right edge
        if (infoX + 400 > window.innerWidth) {
            infoX = this.x - 420;
        }
        // Adjust if too close to bottom edge
        if (infoY + 200 > window.innerHeight) {
            infoY = this.y - 220;
        }
        
        this.inspectInfoBox.style.left = infoX + 'px';
        this.inspectInfoBox.style.top = infoY + 'px';
        this.inspectInfoBox.style.display = '';
        
        // Position highlight box
        this.inspectHighlight.style.left = rect.left + 'px';
        this.inspectHighlight.style.top = rect.top + 'px';
        this.inspectHighlight.style.width = rect.width + 'px';
        this.inspectHighlight.style.height = rect.height + 'px';
        this.inspectHighlight.style.display = '';
    };
    
    // Update immediately
    mouseEmu.updatePosition();
    
    console.log('[Brow6el] INSPECT_MODE:ON');
})();
