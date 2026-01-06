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
        mode: 'normal', // 'precision', 'normal', 'fast'
        dragging: false, // drag and drop state
        draggedElement: null, // element being dragged
        dragGhost: null, // visual clone of dragged element
        lastDragOverElement: null, // track last element for dragleave
        flashTimer: null, // Track flash animation timer
        
        // Speed and color settings for each mode
        modes: {
            precision: { step: 5, color: 'rgba(0, 150, 255, 0.9)', label: 'PRECISION' },
            normal:    { step: 20, color: 'rgba(255, 255, 0, 0.9)', label: 'NORMAL' },
            fast:      { step: 100, color: 'rgba(0, 255, 0, 0.9)', label: 'FAST' }
        },
        
        dragColor: 'rgba(255, 0, 255, 0.9)', // Magenta for dragging
        
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
                
                // Update drag ghost position
                if (this.dragging && this.dragGhost) {
                    this.updateDragGhost();
                }
                
                // If dragging, trigger drag events
                if (this.dragging && this.draggedElement) {
                    // Hide cursor to get element underneath
                    this.cursor.style.display = 'none';
                    const targetEl = document.elementFromPoint(this.x, this.y);
                    this.cursor.style.display = '';
                    
                    // Trigger drag event on dragged element
                    const dragEvent = new DragEvent('drag', {
                        bubbles: true,
                        cancelable: true,
                        clientX: this.x,
                        clientY: this.y
                    });
                    this.draggedElement.dispatchEvent(dragEvent);
                    
                    // Handle dragleave if we moved to a different element
                    if (this.lastDragOverElement && this.lastDragOverElement !== targetEl) {
                        const dragLeaveEvent = new DragEvent('dragleave', {
                            bubbles: true,
                            cancelable: true,
                            clientX: this.x,
                            clientY: this.y
                        });
                        this.lastDragOverElement.dispatchEvent(dragLeaveEvent);
                    }
                    
                    // Trigger dragover on target element
                    if (targetEl) {
                        const dragOverEvent = new DragEvent('dragover', {
                            bubbles: true,
                            cancelable: true,
                            clientX: this.x,
                            clientY: this.y,
                            dataTransfer: new DataTransfer()
                        });
                        targetEl.dispatchEvent(dragOverEvent);
                    }
                    
                    this.lastDragOverElement = targetEl;
                }
                
                // Send position to C++ so it can use CEF mouse events
                console.log('[Brow6el] MOUSE_EMU_POS:' + this.x + ',' + this.y);
            }
        },
        
        // Update drag ghost position
        updateDragGhost: function() {
            if (this.dragGhost) {
                this.dragGhost.style.left = (this.x + 15) + 'px';
                this.dragGhost.style.top = (this.y + 15) + 'px';
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
        
        // Start drag
        startDrag: function() {
            if (this.dragging) return;
            
            this.dragging = true;
            
            // Get element at current position
            if (this.cursor) {
                this.cursor.style.display = 'none';
            }
            
            const el = document.elementFromPoint(this.x, this.y);
            
            if (this.cursor) {
                this.cursor.style.display = '';
                this.cursor.style.background = this.dragColor;
            }
            
            if (el && el.draggable) {
                this.draggedElement = el;
                
                // Create visual ghost/clone
                this.dragGhost = el.cloneNode(true);
                this.dragGhost.style.cssText = `
                    position: fixed !important;
                    pointer-events: none !important;
                    z-index: 2147483646 !important;
                    opacity: 0.7 !important;
                    transform: scale(0.8) !important;
                `;
                document.body.appendChild(this.dragGhost);
                this.updateDragGhost();
                
                // Trigger dragstart event
                const dragStartEvent = new DragEvent('dragstart', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: new DataTransfer()
                });
                el.dispatchEvent(dragStartEvent);
            }
            
            console.log('[Brow6el] MOUSE_EMU_DRAG_START');
        },
        
        // End drag
        endDrag: function() {
            if (!this.dragging) return;
            
            this.dragging = false;
            
            // Remove drag ghost
            if (this.dragGhost) {
                this.dragGhost.remove();
                this.dragGhost = null;
            }
            
            // Get element at drop position
            if (this.cursor) {
                this.cursor.style.display = 'none';
            }
            
            const dropTarget = document.elementFromPoint(this.x, this.y);
            
            if (this.cursor) {
                this.cursor.style.display = '';
                this.cursor.style.background = this.modes[this.mode].color;
            }
            
            if (dropTarget && this.draggedElement) {
                // Trigger drop event
                const dropEvent = new DragEvent('drop', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: new DataTransfer()
                });
                dropTarget.dispatchEvent(dropEvent);
                
                // Trigger dragend on original element
                const dragEndEvent = new DragEvent('dragend', {
                    bubbles: true,
                    cancelable: true
                });
                this.draggedElement.dispatchEvent(dragEndEvent);
                
                this.draggedElement = null;
            }
            
            // Clear last drag over element
            this.lastDragOverElement = null;
            
            console.log('[Brow6el] MOUSE_EMU_DRAG_END');
        },
        
        // Toggle drag state
        toggleDrag: function() {
            if (this.dragging) {
                this.endDrag();
            } else {
                this.startDrag();
            }
        },
        
        // Flash cursor red to show click
        flashClick: function() {
            if (!this.cursor) return;
            
            // Clear any existing flash timer to prevent color mixing
            if (this.flashTimer) {
                clearTimeout(this.flashTimer);
                this.flashTimer = null;
            }
            
            const originalColor = this.modes[this.mode].color;
            this.cursor.style.background = 'rgba(255, 0, 0, 0.9)';
            
            this.flashTimer = setTimeout(() => {
                if (this.cursor) {
                    this.cursor.style.background = originalColor;
                }
                this.flashTimer = null;
            }, 100);
        },
        
        // Set speed mode
        setMode: function(newMode) {
            if (this.modes[newMode]) {
                this.mode = newMode;
                this.step = this.modes[newMode].step;
                if (this.cursor) {
                    this.cursor.style.background = this.modes[newMode].color;
                }
                console.log('[Brow6el] MOUSE_EMU_MODE:' + newMode);
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
        
        // Handle keys
        handleKey: function(key) {
            switch(key) {
                // WASD controls (primary)
                case 'w':
                case 'W':
                    this.move(0, -this.step);
                    return true;
                case 'a':
                case 'A':
                    this.move(-this.step, 0);
                    return true;
                case 's':
                case 'S':
                    this.move(0, this.step);
                    return true;
                case 'd':
                case 'D':
                    this.move(this.step, 0);
                    return true;
                    
                // Arrow keys (fallback)
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
                    
                // Speed mode toggles
                case 'q':
                case 'Q':
                    // Toggle precision mode
                    this.setMode(this.mode === 'precision' ? 'normal' : 'precision');
                    return true;
                case 'f':
                case 'F':
                    // Toggle fast mode
                    this.setMode(this.mode === 'fast' ? 'normal' : 'fast');
                    return true;
                
                // Drag and drop toggle
                case 'r':
                case 'R':
                    this.toggleDrag();
                    return true;
                    
                // Click actions (also end drag if dragging)
                case 'Enter':
                case ' ':
                    if (this.dragging) {
                        // If dragging, end the drag (drop)
                        this.endDrag();
                    } else {
                        // Otherwise, click
                        this.click();
                    }
                    return true;
                case 'e':
                case 'E':
                    this.click();
                    return true;
            }
            return false;
        },
        
        // Remove cursor
        cleanup: function() {
            if (this.flashTimer) {
                clearTimeout(this.flashTimer);
                this.flashTimer = null;
            }
            if (this.cursor) {
                this.cursor.remove();
                this.cursor = null;
            }
            // Cleanup inspect mode elements if active
            if (this.inspectInfoBox) {
                this.inspectInfoBox.remove();
                this.inspectInfoBox = null;
            }
            if (this.inspectHighlight) {
                this.inspectHighlight.remove();
                this.inspectHighlight = null;
            }
            this.inspectMode = false;
            this.lastInspectedElement = null;
            console.log('[Brow6el] MOUSE_EMU_CLOSED');
        }
    };
    
    window.__brow6el_mouse_emu = mouseEmu;
    mouseEmu.show();
})();
