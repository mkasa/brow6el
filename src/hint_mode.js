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
            
            // Search main document only
            // Note: Iframes are skipped because their coordinates don't align with main window
            findInDocument(document);
            
            // Convert Set to Array
            return Array.from(visibleSet);
        },
        
        // Create overlay labels
        show: function() {
            this.elements = this.findElements();
            
            // Create a dedicated container for all hints at the root level
            // This ensures hints are above all page content and stacking contexts
            let container = document.getElementById('__brow6el_hint_container');
            if (!container) {
                container = document.createElement('div');
                container.id = '__brow6el_hint_container';
                container.style.cssText = `
                    position: fixed !important;
                    top: 0 !important;
                    left: 0 !important;
                    width: 100% !important;
                    height: 100% !important;
                    z-index: 2147483647 !important;
                    pointer-events: none !important;
                    transform: translateZ(0) !important;
                    isolation: isolate !important;
                `;
                // Always append to documentElement (html), not body
                // This avoids stacking context issues from body styles
                (document.documentElement || document.body).appendChild(container);
            }
            
            this.overlays.push(container); // Track for cleanup
            
            // If no elements found and we're in a frameset, try the main frame
            if (this.elements.length === 0 && window.frames.length > 0) {
                console.log('[Brow6el] In frameset, no elements in main document');
                return;
            }
            
            this.elements.forEach((el, index) => {
                const rect = el.getBoundingClientRect();
                const label = this.generateLabel(index);
                
                const overlay = document.createElement('div');
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
                    z-index: 2147483647 !important;
                    pointer-events: none !important;
                    line-height: 1 !important;
                    display: block !important;
                    visibility: visible !important;
                    opacity: 1 !important;
                `;
                
                // Try to append to body, or html if body doesn't exist
                const target = document.body || document.documentElement;
                target.appendChild(overlay);
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
