// Mouse emulation mode for keyboard-driven mouse control
(function() {
    // Skip if in a frameset without content
    if (window.frames.length > 0 && document.body && document.body.children.length === 0) {
        console.log('[Brow6el] MOUSE_EMU_FRAMESET - Mouse emulation not available in framesets');
        return;
    }
    
    // Remove existing cursor if any
    if (window.__brow6el_mouse_emu) {
        window.__brow6el_mouse_emu.cleanup();
    }
    
    const mouseEmu = {
        cursor: null,
        x: 0,
        y: 0,
        step: 20, // pixels to move per keypress
        
        // Create yellow circle cursor
        show: function() {
            // Start at center of viewport
            this.x = window.innerWidth / 2;
            this.y = window.innerHeight / 2;
            
            this.cursor = document.createElement('div');
            this.cursor.style.cssText = `
                position: fixed !important;
                width: 20px !important;
                height: 20px !important;
                border-radius: 50% !important;
                background: rgba(255, 255, 0, 0.9) !important;
                border: 3px solid #000 !important;
                z-index: 2147483647 !important;
                pointer-events: none !important;
                box-shadow: 0 0 10px rgba(0, 0, 0, 0.5) !important;
                display: block !important;
                visibility: visible !important;
                opacity: 1 !important;
            `;
            
            // Try to append to body, or documentElement if body doesn't exist
            const target = document.body || document.documentElement;
            if (!target) {
                console.log('[Brow6el] MOUSE_EMU_ERROR - No document body or element available');
                return;
            }
            target.appendChild(this.cursor);
            this.updatePosition();
            
            console.log('[Brow6el] MOUSE_EMU_ACTIVE');
        },
        
        // Update cursor position
        updatePosition: function() {
            if (this.cursor) {
                this.cursor.style.left = (this.x - 10) + 'px';
                this.cursor.style.top = (this.y - 10) + 'px';
                
                // Send position to C++ so it can use CEF mouse events
                console.log('[Brow6el] MOUSE_EMU_POS:' + this.x + ',' + this.y);
            }
        },
        
        // Move cursor
        move: function(dx, dy) {
            this.x += dx;
            this.y += dy;
            
            // Clamp to viewport
            if (this.x < 0) this.x = 0;
            if (this.y < 0) this.y = 0;
            if (this.x > window.innerWidth) this.x = window.innerWidth;
            if (this.y > window.innerHeight) this.y = window.innerHeight;
            
            this.updatePosition();
        },
        
        // Simulate click at current position
        click: function() {
            // This is now handled by C++ using CEF mouse events
            // But for SELECT elements, we just focus them instead
            const info = this.getElementType();
            
            if (info) {
                if (info.isSelect || info.isInput) {
                    // For select/input elements, just focus them - don't click
                    // The select_detector.js will handle showing the options
                    info.element.focus();
                    console.log('[Brow6el] MOUSE_EMU_FOCUS:' + info.tagName);
                    this.flashClick();
                } else {
                    // For other elements, tell C++ to send real click via CEF
                    console.log('[Brow6el] MOUSE_EMU_CLICK');
                    this.flashClick();
                }
            }
        },
        
        // Flash cursor red to show click
        flashClick: function() {
            if (this.cursor) {
                this.cursor.style.background = 'rgba(255, 0, 0, 0.7)';
                setTimeout(() => {
                    if (this.cursor) {
                        this.cursor.style.background = 'rgba(255, 255, 0, 0.7)';
                    }
                }, 100);
            }
        },
        
        // Get element type at cursor position
        getElementType: function() {
            // Temporarily hide cursor
            if (this.cursor) {
                this.cursor.style.display = 'none';
            }
            
            const el = document.elementFromPoint(this.x, this.y);
            
            // Restore cursor
            if (this.cursor) {
                this.cursor.style.display = '';
            }
            
            if (el) {
                return {
                    element: el,
                    tagName: el.tagName,
                    type: el.type || '',
                    isSelect: el.tagName === 'SELECT',
                    isInput: el.tagName === 'INPUT' || el.tagName === 'TEXTAREA'
                };
            }
            return null;
        },
        
        // Handle arrow keys
        handleKey: function(key) {
            switch(key) {
                case 'ArrowUp':
                    this.move(0, -this.step);
                    return true;
                case 'ArrowDown':
                    this.move(0, this.step);
                    return true;
                case 'ArrowLeft':
                    this.move(-this.step, 0);
                    return true;
                case 'ArrowRight':
                    this.move(this.step, 0);
                    return true;
                case 'Enter':
                    this.click();
                    return true;
            }
            return false;
        },
        
        // Remove cursor
        cleanup: function() {
            if (this.cursor) {
                this.cursor.remove();
                this.cursor = null;
            }
            console.log('[Brow6el] MOUSE_EMU_CLOSED');
        }
    };
    
    window.__brow6el_mouse_emu = mouseEmu;
    mouseEmu.show();
})();
