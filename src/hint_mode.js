// Hint mode for keyboard navigation
(function() {
    // Skip if in a frameset without content
    if (window.frames.length > 0 && document.body && document.body.children.length === 0) {
        console.log('[Brow6el] HINT_MODE_FRAMESET - Hint mode not available in framesets, use Ctrl+E (mouse emulation) instead');
        return;
    }
    
    // Remove existing hints if any
    if (window.__brow6el_hints) {
        window.__brow6el_hints.cleanup();
    }
    
    const hints = {
        elements: [],
        overlays: [],
        
        // Generate hint labels: a, b, c, ... z, aa, ab, ...
        generateLabel: function(index) {
            let label = '';
            let n = index;
            do {
                label = String.fromCharCode(97 + (n % 26)) + label;
                n = Math.floor(n / 26) - 1;
            } while (n >= 0);
            return label;
        },
        
        // Find all clickable elements
        findElements: function() {
            // Comprehensive selector for interactive elements
            const selector = [
                'a[href]',                              // Links
                'button',                                // Buttons
                'input[type="button"]',                  // Button inputs
                'input[type="submit"]',                  // Submit inputs
                'input[type="reset"]',                   // Reset inputs
                'input[type="image"]',                   // Image inputs
                '[role="button"]',                       // ARIA buttons
                '[role="link"]',                         // ARIA links
                '[onclick]',                             // Elements with onclick
                'summary',                               // Details/summary
                '[tabindex]:not([tabindex="-1"])',      // Focusable elements (but not tabindex=-1)
                'label[for]',                            // Labels (clickable)
            ].join(', ');
            
            // Use Set to automatically deduplicate elements
            const visibleSet = new Set();
            
            // Helper to check if element is clickable via cursor style
            const hasPointerCursor = (el) => {
                const style = window.getComputedStyle(el);
                return style.cursor === 'pointer' || style.cursor === 'grab';
            };
            
            // Helper to check if element is already covered by a parent in the set
            const hasClickableParent = (el, set) => {
                let parent = el.parentElement;
                while (parent) {
                    if (set.has(parent)) return true;
                    parent = parent.parentElement;
                }
                return false;
            };
            
            // Helper to find elements in a document
            const findInDocument = (doc) => {
                try {
                    const all = doc.querySelectorAll(selector);
                    all.forEach((el) => {
                        // Skip if already in set (handles multiple selector matches)
                        if (visibleSet.has(el)) return;
                        
                        const rect = el.getBoundingClientRect();
                        const style = window.getComputedStyle(el);
                        
                        // Skip if element is too small (likely hidden or decorative)
                        if (rect.width < 3 || rect.height < 3) return;
                        
                        // Check if element is visible and in viewport
                        if (style.visibility === 'hidden' || style.display === 'none') return;
                        if (style.opacity === '0') return;
                        
                        // Check viewport visibility
                        if (rect.bottom < 0 || rect.top > window.innerHeight) return;
                        if (rect.right < 0 || rect.left > window.innerWidth) return;
                        
                        // Check if element is actually visible (not covered)
                        const centerX = rect.left + rect.width / 2;
                        const centerY = rect.top + rect.height / 2;
                        const elementAtPoint = document.elementFromPoint(centerX, centerY);
                        
                        // Element is visible if we hit it or one of its descendants
                        if (elementAtPoint && (elementAtPoint === el || el.contains(elementAtPoint))) {
                            visibleSet.add(el);
                        }
                    });
                    
                    // Also find elements with cursor: pointer that aren't in selector
                    // Common for SPAs with click handlers on divs/spans
                    const allElements = doc.querySelectorAll('div, span, li, td, th');
                    allElements.forEach((el) => {
                        // Skip if already found
                        if (visibleSet.has(el)) return;
                        
                        // Skip if has clickable parent (avoid nested duplicates)
                        if (hasClickableParent(el, visibleSet)) return;
                        
                        const rect = el.getBoundingClientRect();
                        const style = window.getComputedStyle(el);
                        
                        // Must have pointer cursor
                        if (!hasPointerCursor(el)) return;
                        
                        // Skip if too small
                        if (rect.width < 10 || rect.height < 10) return;
                        
                        // Check visibility
                        if (style.visibility === 'hidden' || style.display === 'none') return;
                        if (style.opacity === '0') return;
                        
                        // Check viewport visibility
                        if (rect.bottom < 0 || rect.top > window.innerHeight) return;
                        if (rect.right < 0 || rect.left > window.innerWidth) return;
                        
                        // Check if element is actually visible
                        const centerX = rect.left + rect.width / 2;
                        const centerY = rect.top + rect.height / 2;
                        const elementAtPoint = document.elementFromPoint(centerX, centerY);
                        
                        if (elementAtPoint && (elementAtPoint === el || el.contains(elementAtPoint))) {
                            visibleSet.add(el);
                        }
                    });
                } catch (e) {
                    // Ignore cross-origin iframe errors
                }
            };
            
            // Search main document
            findInDocument(document);
            
            // Also search same-origin iframes (for cookie dialogs, modals, etc.)
            const iframes = document.querySelectorAll('iframe');
            iframes.forEach((iframe) => {
                let foundElements = false;
                try {
                    // Try to access iframe document (will fail for cross-origin)
                    const iframeDoc = iframe.contentDocument || iframe.contentWindow?.document;
                    if (iframeDoc && iframeDoc.body) {
                        // Get iframe position in main viewport
                        const iframeRect = iframe.getBoundingClientRect();
                        
                        // Search for elements in iframe
                        const iframeSelector = [
                            'a[href]', 'button', 'input[type="button"]', 'input[type="submit"]',
                            'input[type="reset"]', 'input[type="image"]', '[role="button"]',
                            '[role="link"]', '[onclick]', 'summary', '[tabindex]:not([tabindex="-1"])',
                            'label[for]'
                        ].join(', ');
                        
                        const iframeElements = iframeDoc.querySelectorAll(iframeSelector);
                        iframeElements.forEach((el) => {
                            if (visibleSet.has(el)) return;
                            
                            // Get element position relative to iframe
                            const rect = el.getBoundingClientRect();
                            const style = iframeDoc.defaultView.getComputedStyle(el);
                            
                            // Skip if too small
                            if (rect.width < 3 || rect.height < 3) return;
                            
                            // Check visibility
                            if (style.visibility === 'hidden' || style.display === 'none') return;
                            if (style.opacity === '0') return;
                            
                            // Translate coordinates to main viewport
                            const mainX = iframeRect.left + rect.left;
                            const mainY = iframeRect.top + rect.top;
                            
                            // Check if visible in main viewport
                            if (mainY + rect.height < 0 || mainY > window.innerHeight) return;
                            if (mainX + rect.width < 0 || mainX > window.innerWidth) return;
                            
                            // Check if element is actually visible using main document coordinates
                            const centerX = mainX + rect.width / 2;
                            const centerY = mainY + rect.height / 2;
                            const elementAtPoint = document.elementFromPoint(centerX, centerY);
                            
                            // If we hit the iframe or something in it, the element is potentially visible
                            if (elementAtPoint && (elementAtPoint === iframe || iframe.contains(elementAtPoint))) {
                                visibleSet.add(el);
                                foundElements = true;
                            }
                        });
                    }
                } catch (e) {
                    // Cross-origin iframe - we cannot access its contents
                    // Fall through to make the iframe itself a hint target
                }
                
                // For cross-origin iframes (or empty same-origin iframes), make the iframe itself clickable
                // This indicates to the user that there's an iframe, though they'll need mouse mode to interact with its contents
                if (!foundElements) {
                    const rect = iframe.getBoundingClientRect();
                    const style = window.getComputedStyle(iframe);
                    
                    // Only add if iframe is reasonably sized and visible
                    if (rect.width > 100 && rect.height > 100 &&
                        style.visibility !== 'hidden' && style.display !== 'none' &&
                        style.opacity !== '0') {
                        
                        // Check if visible in viewport
                        if (rect.bottom > 0 && rect.top < window.innerHeight &&
                            rect.right > 0 && rect.left < window.innerWidth) {
                            // Mark this as a cross-origin iframe for special rendering
                            iframe.__brow6el_crossorigin_iframe = true;
                            visibleSet.add(iframe);
                        }
                    }
                }
            });
            
            // Convert Set to Array
            return Array.from(visibleSet);
        },
        
        // Create overlay labels
        show: function() {
            this.elements = this.findElements();
            
            // Always remove and recreate container to ensure it's last in DOM order
            // This is crucial for beating cookie dialogs that also use max z-index
            let oldContainer = document.getElementById('__brow6el_hint_container');
            if (oldContainer) {
                oldContainer.remove();
            }
            
            // Create a dedicated container for all hints at the root level
            // This ensures hints are above all page content and stacking contexts
            const container = document.createElement('div');
            container.id = '__brow6el_hint_container';
            container.style.cssText = `
                all: initial !important;
                position: fixed !important;
                top: 0 !important;
                left: 0 !important;
                right: 0 !important;
                bottom: 0 !important;
                width: 100vw !important;
                height: 100vh !important;
                z-index: 2147483647 !important;
                pointer-events: none !important;
                transform: translateZ(999999px) !important;
                isolation: isolate !important;
                mix-blend-mode: normal !important;
                filter: none !important;
                backdrop-filter: none !important;
                clip-path: none !important;
                mask: none !important;
                contain: none !important;
                display: block !important;
                visibility: visible !important;
                opacity: 1 !important;
            `;
            // Always append to documentElement (html), not body
            // This avoids stacking context issues from body styles
            (document.documentElement || document.body).appendChild(container);
            
            this.overlays.push(container); // Track for cleanup
            
            // If no elements found and we're in a frameset, try the main frame
            if (this.elements.length === 0 && window.frames.length > 0) {
                console.log('[Brow6el] In frameset, no elements in main document');
                return;
            }
            
            this.elements.forEach((el, index) => {
                // Check if this is a cross-origin iframe - show warning only, no hint label
                const isCrossOriginIframe = el.__brow6el_crossorigin_iframe;
                
                // Check if element is in an iframe
                let rect = el.getBoundingClientRect();
                let ownerDoc = el.ownerDocument;
                
                // If element is in an iframe, translate coordinates to main viewport
                if (ownerDoc !== document) {
                    // Find the iframe containing this element
                    const iframes = document.querySelectorAll('iframe');
                    for (let iframe of iframes) {
                        try {
                            if (iframe.contentDocument === ownerDoc || iframe.contentWindow?.document === ownerDoc) {
                                const iframeRect = iframe.getBoundingClientRect();
                                // Translate iframe-relative coordinates to main viewport
                                rect = {
                                    left: iframeRect.left + rect.left,
                                    top: iframeRect.top + rect.top,
                                    right: iframeRect.left + rect.right,
                                    bottom: iframeRect.top + rect.bottom,
                                    width: rect.width,
                                    height: rect.height
                                };
                                break;
                            }
                        } catch (e) {
                            // Cross-origin - skip
                        }
                    }
                }
                
                const overlay = document.createElement('div');
                
                if (isCrossOriginIframe) {
                    // Show iframe URL instead of hint label
                    let iframeInfo = '';
                    if (el.src) {
                        try {
                            const url = new URL(el.src);
                            const displayUrl = url.hostname + url.pathname;
                            iframeInfo = displayUrl.length > 30 ? displayUrl.substring(0, 27) + '...' : displayUrl;
                        } catch (e) {
                            iframeInfo = el.src.length > 30 ? el.src.substring(0, 27) + '...' : el.src;
                        }
                    } else {
                        iframeInfo = 'cross-origin';
                    }
                    
                    overlay.innerHTML = `<div style="font-size: 10px; margin-bottom: 3px; color: #600; font-weight: bold; letter-spacing: 0.5px;">CROSS-ORIGIN IFRAME</div><div style="font-size: 9px; margin-bottom: 3px; word-break: break-all; line-height: 1.1;">${iframeInfo}</div><div style="font-size: 10px; font-weight: bold;">⚠ Use mouse mode (E)</div>`;
                    overlay.style.cssText = `
                        position: fixed !important;
                        left: ${rect.left + rect.width / 2 - 60}px !important;
                        top: ${rect.top + rect.height / 2 - 35}px !important;
                        width: 120px !important;
                        background: rgba(250, 128, 114, 0.95) !important;
                        color: #000 !important;
                        border: 3px solid #8B0000 !important;
                        padding: 8px 6px !important;
                        font-family: monospace !important;
                        font-size: 14px !important;
                        font-weight: normal !important;
                        pointer-events: none !important;
                        line-height: 1.2 !important;
                        display: block !important;
                        visibility: visible !important;
                        opacity: 1 !important;
                        text-align: center !important;
                        border-radius: 5px !important;
                        box-shadow: 0 2px 8px rgba(0,0,0,0.3) !important;
                    `;
                } else {
                    // Normal hint - generate label
                    const label = this.generateLabel(index);
                    overlay.textContent = label;
                    overlay.style.cssText = `
                        position: fixed !important;
                        left: ${rect.left}px !important;
                        top: ${rect.top}px !important;
                        background: rgba(255, 255, 0, 0.9) !important;
                        color: #000 !important;
                        border: 2px solid #000 !important;
                        padding: 2px 4px !important;
                        font-family: monospace !important;
                        font-size: 12px !important;
                        font-weight: bold !important;
                        pointer-events: none !important;
                        line-height: 1 !important;
                        display: block !important;
                        visibility: visible !important;
                        opacity: 1 !important;
                    `;
                }
                
                // Append to container to inherit stacking context
                container.appendChild(overlay);
                this.overlays.push(overlay);
            });
            
            // Send hint count to console for status bar
            console.log('[Brow6el] HINT_MODE_ACTIVE:' + this.elements.length);
        },
        
        // Select element by label
        select: function(label) {
            label = label.toLowerCase();
            
            for (let i = 0; i < this.elements.length; i++) {
                if (this.generateLabel(i) === label) {
                    const el = this.elements[i];
                    
                    // Check if element is a cross-origin iframe
                    if (el.tagName === 'IFRAME') {
                        try {
                            // Try to access - if this succeeds, it's same-origin
                            const iframeDoc = el.contentDocument || el.contentWindow?.document;
                            if (!iframeDoc) {
                                // Cross-origin iframe
                                console.log('[Brow6el] HINT_IFRAME_CROSSORIGIN - Use mouse mode (E key) to interact with cross-origin iframes');
                                this.cleanup();
                                return true;
                            }
                        } catch (e) {
                            // Cross-origin iframe
                            console.log('[Brow6el] HINT_IFRAME_CROSSORIGIN - Use mouse mode (E key) to interact with cross-origin iframes');
                            this.cleanup();
                            return true;
                        }
                    }
                    
                    // Always use click() to simulate real mouse click
                    // This ensures all event listeners fire (onclick, addEventListener)
                    // and allows preventDefault() to work properly
                    el.click();
                    console.log('[Brow6el] HINT_CLICKED:' + label + ' on ' + el.tagName);
                    
                    this.cleanup();
                    return true;
                }
            }
            
            console.log('[Brow6el] HINT_NOT_FOUND:' + label);
            return false;
        },
        
        // Remove all overlays
        cleanup: function() {
            this.overlays.forEach(overlay => overlay.remove());
            this.overlays = [];
            this.elements = [];
            
            // Disconnect MutationObserver to stop watching DOM changes
            if (window.__brow6el_mutation_observer) {
                window.__brow6el_mutation_observer.disconnect();
                window.__brow6el_mutation_observer = null;
            }
            
            console.log('[Brow6el] HINT_MODE_CLOSED');
        }
    };
    
    window.__brow6el_hints = hints;
    hints.show();
    
    // For Kitty: Watch for DOM changes to trigger repaint
    // Overlays change CSS (style attribute) which CEF may not immediately detect
    if (!window.__brow6el_mutation_observer) {
        window.__brow6el_mutation_observer = new MutationObserver(function(mutations) {
            // Signal DOM changed for Kitty renderer
            console.log('[Brow6el] DOM_CHANGED');
        });
        window.__brow6el_mutation_observer.observe(document.body, {
            attributes: true,
            childList: true,
            subtree: true,
            attributeFilter: ['style'] // Watch for style changes (overlay positioning)
        });
        
        // Trigger initial repaint for overlay visibility (using requestAnimationFrame to let browser process DOM)
        requestAnimationFrame(function() {
            console.log('[Brow6el] DOM_CHANGED');
        });
    }
})();
