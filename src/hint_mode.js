// Hint mode for keyboard navigation
(function() {
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
        
        // Find all links
        findElements: function() {
            // Only target links with href for now
            const selector = 'a[href]';
            const visible = [];
            
            // Helper to find elements in a document
            const findInDocument = (doc) => {
                try {
                    const all = doc.querySelectorAll(selector);
                    all.forEach((el) => {
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
                            visible.push(el);
                        }
                    });
                } catch (e) {
                    // Ignore cross-origin iframe errors
                }
            };
            
            // Search main document only
            // Note: Iframes are skipped because their coordinates don't align with main window
            findInDocument(document);
            
            return visible;
        },
        
        // Create overlay labels
        show: function() {
            this.elements = this.findElements();
            
            this.elements.forEach((el, index) => {
                const rect = el.getBoundingClientRect();
                const label = this.generateLabel(index);
                
                const overlay = document.createElement('div');
                overlay.textContent = label;
                overlay.style.cssText = `
                    position: fixed;
                    left: ${rect.left}px;
                    top: ${rect.top}px;
                    background: rgba(255, 255, 0, 0.4);
                    color: #000;
                    border: 2px solid #000;
                    padding: 2px 4px;
                    font-family: monospace;
                    font-size: 12px;
                    font-weight: bold;
                    z-index: 2147483647;
                    pointer-events: none;
                    line-height: 1;
                `;
                
                document.body.appendChild(overlay);
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
                    
                    // Just click the link
                    el.click();
                    console.log('[Brow6el] HINT_CLICKED:' + label);
                    
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
            console.log('[Brow6el] HINT_MODE_CLOSED');
        }
    };
    
    window.__brow6el_hints = hints;
    hints.show();
})();
