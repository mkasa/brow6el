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
        
        // Grid jump mode
        gridMode: false, // whether grid overlay is active
        gridOverlays: [], // grid cell overlays
        gridCells: [], // grid cell coordinates
        gridBaseX: 0, // base coordinates for current grid
        gridBaseY: 0,
        gridBaseWidth: 0,
        gridBaseHeight: 0,
        gridZoomHistory: [], // stack of previous grid states for backspace navigation
        
        // Test if label would overlap with cell borders by checking actual geometry
        wouldLabelOverlapBorder: function(cellWidth, cellHeight) {
            // Label: 30px width + 2px border = 34px total diameter
            // But border-radius: 50% makes it circular, and some visual overlap is acceptable
            // Use 15px radius (just the content, not the border) for a less conservative check
            
            const labelRadius = 15; // Just the content radius, allowing border to potentially touch
            const cellBorderWidth = 2;
            
            // Distance from cell center to inner edge of cell border
            const clearanceX = (cellWidth / 2) - cellBorderWidth;
            const clearanceY = (cellHeight / 2) - cellBorderWidth;
            
            // Label overlaps if its content radius exceeds the clearance
            return (clearanceX < labelRadius || clearanceY < labelRadius);
        },
        
        // Calculate grid size - pick configuration that allows deepest zoom
        calculateGridSize: function(width, height) {
            // Get number of available grid keys (default 9)
            const gridKeys = window.__brow6el_grid_keys || 'qweasdzxc';
            const maxCells = gridKeys.length;
            
            // Try grid configurations in order of preference
            const configs = [
                { cols: 3, rows: 3 }, // 9 cells - primary choice, matches qweasdzxc layout
                { cols: 3, rows: 2 }, // 6 cells
                { cols: 2, rows: 3 }, // 6 cells
                { cols: 2, rows: 2 }, // 4 cells - square
                { cols: 3, rows: 1 }, // 3 cells - horizontal
                { cols: 1, rows: 3 }, // 3 cells - vertical
                { cols: 2, rows: 1 }, // 2 cells - horizontal
                { cols: 1, rows: 2 }, // 2 cells - vertical
            ];
            
            // Find the best config - just use the first one where labels fit
            for (const config of configs) {
                const numCells = config.cols * config.rows;
                
                // Skip if we don't have enough keys for this grid
                if (numCells > maxCells) continue;
                
                const cellWidth = width / config.cols;
                const cellHeight = height / config.rows;
                
                // Check if current cells would have overlapping labels
                if (!this.wouldLabelOverlapBorder(cellWidth, cellHeight)) {
                    return config;
                }
            }
            
            // Absolute fallback
            return { cols: 1, rows: 1 };
        },
        
        // Generate hint labels for grid: use configured keys or default to qweasdzxc
        generateGridLabel: function(index) {
            const gridKeys = window.__brow6el_grid_keys || 'qweasdzxc';
            return gridKeys[index] || '';
        },
        
        // Show grid overlay
        showGrid: function() {
            if (this.gridMode) return; // Already showing
            
            this.gridMode = true;
            
            // Use current viewport or sub-region
            const baseX = this.gridBaseWidth > 0 ? this.gridBaseX : 0;
            const baseY = this.gridBaseHeight > 0 ? this.gridBaseY : 0;
            const width = this.gridBaseWidth > 0 ? this.gridBaseWidth : window.innerWidth;
            const height = this.gridBaseHeight > 0 ? this.gridBaseHeight : window.innerHeight;
            
            // Check if area is too small to divide at all
            if (this.wouldLabelOverlapBorder(width, height)) {
                // Area is too small even for a single label - just click center
                this.gridMode = false;
                this.x = baseX + width / 2;
                this.y = baseY + height / 2;
                this.updatePosition();
                this.click();
                
                // Reset zoom state
                this.gridBaseX = 0;
                this.gridBaseY = 0;
                this.gridBaseWidth = 0;
                this.gridBaseHeight = 0;
                this.gridZoomHistory = [];
                
                console.log('[Brow6el] MOUSE_EMU_GRID_TOO_SMALL_AUTO_CLICK');
                return;
            }
            
            const { cols, rows } = this.calculateGridSize(width, height);
            
            // If we got a 1x1 grid, just position cursor at center and exit grid mode
            if (cols === 1 && rows === 1) {
                this.gridMode = false;
                
                // Hide grid overlays first
                this.gridOverlays.forEach(overlay => overlay.remove());
                this.gridOverlays = [];
                this.gridCells = [];
                
                // Switch to precision mode BEFORE moving cursor
                this.setMode('precision');
                
                // Now position cursor at center
                this.x = baseX + width / 2;
                this.y = baseY + height / 2;
                this.updatePosition();
                
                console.log('[Brow6el] MOUSE_EMU_GRID_CLOSED');
                return;
            }
            
            // Always remove and recreate container to ensure it's last in DOM order
            // This is crucial for beating cookie dialogs that also use max z-index
            let oldContainer = document.getElementById('__brow6el_grid_container');
            if (oldContainer) {
                oldContainer.remove();
            }
            
            // Create a dedicated container for all grid elements at the root level
            // This ensures grid is above all page content and stacking contexts
            const container = document.createElement('div');
            container.id = '__brow6el_grid_container';
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
            
            this.gridOverlays.push(container); // Track for cleanup
            
            const cellWidth = width / cols;
            const cellHeight = height / rows;
            
            // Check if we're at maximum zoom: can this cell be subdivided at all?
            // If the cell itself is too small for a label, it's max zoom (will auto-click)
            const atMaxZoom = this.wouldLabelOverlapBorder(cellWidth, cellHeight);
            const gridColor = atMaxZoom ? 'rgba(255, 50, 50, 0.7)' : 'rgba(0, 255, 150, 0.7)';
            const labelColor = atMaxZoom ? 'rgba(255, 50, 50, 0.9)' : 'rgba(0, 255, 150, 0.9)';
            
            this.gridCells = [];
            let index = 0;
            
            for (let row = 0; row < rows; row++) {
                for (let col = 0; col < cols; col++) {
                    const x = baseX + col * cellWidth;
                    const y = baseY + row * cellHeight;
                    const centerX = x + cellWidth / 2;
                    const centerY = y + cellHeight / 2;
                    const label = this.generateGridLabel(index);
                    
                    // Store cell info
                    this.gridCells.push({
                        label: label,
                        x: x,
                        y: y,
                        width: cellWidth,
                        height: cellHeight,
                        centerX: centerX,
                        centerY: centerY
                    });
                    
                    // Create cell border overlay
                    const cellOverlay = document.createElement('div');
                    cellOverlay.style.cssText = `
                        position: fixed !important;
                        left: ${x}px !important;
                        top: ${y}px !important;
                        width: ${cellWidth}px !important;
                        height: ${cellHeight}px !important;
                        border: 2px solid ${gridColor} !important;
                        box-sizing: border-box !important;
                        pointer-events: none !important;
                    `;
                    container.appendChild(cellOverlay);
                    this.gridOverlays.push(cellOverlay);
                    
                    // Create label overlay
                    const labelOverlay = document.createElement('div');
                    labelOverlay.textContent = label;
                    labelOverlay.style.cssText = `
                        position: fixed !important;
                        left: ${centerX - 15}px !important;
                        top: ${centerY - 15}px !important;
                        width: 30px !important;
                        height: 30px !important;
                        background: ${labelColor} !important;
                        color: #000 !important;
                        border: 2px solid #000 !important;
                        border-radius: 50% !important;
                        display: flex !important;
                        align-items: center !important;
                        justify-content: center !important;
                        font-family: monospace !important;
                        font-size: 16px !important;
                        font-weight: bold !important;
                        pointer-events: none !important;
                    `;
                    container.appendChild(labelOverlay);
                    this.gridOverlays.push(labelOverlay);
                    
                    index++;
                    if (index >= 26) break; // Max 26 labels
                }
                if (index >= 26) break;
            }
            
            console.log('[Brow6el] MOUSE_EMU_GRID_ACTIVE:' + this.gridCells.length);
        },
        
        // Jump to grid cell
        jumpToGrid: function(label) {
            const cell = this.gridCells.find(c => c.label === label);
            if (!cell) return false;
            
            // Save current grid state to history before zooming in
            this.gridZoomHistory.push({
                x: this.gridBaseX,
                y: this.gridBaseY,
                width: this.gridBaseWidth,
                height: this.gridBaseHeight
            });
            
            // Move cursor to center of cell
            this.x = cell.centerX;
            this.y = cell.centerY;
            this.updatePosition();
            
            // Suspend MutationObserver during grid rebuild to avoid flicker
            if (window.__brow6el_mutation_observer) {
                window.__brow6el_mutation_observer.disconnect();
            }
            
            // Clear current grid
            this.hideGrid();
            
            // Set up sub-grid for this cell
            this.gridBaseX = cell.x;
            this.gridBaseY = cell.y;
            this.gridBaseWidth = cell.width;
            this.gridBaseHeight = cell.height;
            
            // Show sub-grid automatically (will handle 1x1 case inside showGrid)
            this.showGrid();
            
            // Reconnect MutationObserver after grid rebuild
            // Use requestAnimationFrame to ensure DOM changes are processed first
            if (window.__brow6el_mutation_observer) {
                requestAnimationFrame(function() {
                    window.__brow6el_mutation_observer.observe(document.body, {
                        attributes: true,
                        childList: true,
                        subtree: true,
                        attributeFilter: ['style']
                    });
                    // Trigger repaint after browser processes DOM changes
                    console.log('[Brow6el] DOM_CHANGED');
                });
            }
            
            console.log('[Brow6el] MOUSE_EMU_GRID_JUMP:' + label);
            return true;
        },
        
        // Hide grid overlay
        hideGrid: function() {
            if (!this.gridMode) return;
            
            this.gridMode = false;
            this.gridOverlays.forEach(overlay => overlay.remove());
            this.gridOverlays = [];
            this.gridCells = [];
            
            console.log('[Brow6el] MOUSE_EMU_GRID_CLOSED');
        },
        
        // Reset grid to full viewport
        resetGrid: function() {
            this.hideGrid();
            this.gridBaseX = 0;
            this.gridBaseY = 0;
            this.gridBaseWidth = 0;
            this.gridBaseHeight = 0;
            this.gridZoomHistory = []; // Clear history
            this.showGrid();
        },
        
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
            // Convert CSS pixels to device pixels (account for zoom/devicePixelRatio)
            // JavaScript uses CSS pixels, but CEF expects device pixels
            const devicePixelRatio = window.devicePixelRatio || 1.0;
            const intX = Math.round(this.x * devicePixelRatio);
            const intY = Math.round(this.y * devicePixelRatio);
            
            console.log('[Brow6el] MOUSE_EMU_POS:' + intX + ',' + intY);
            
            // Check if we need special handling for form elements
            // Temporarily hide cursor to detect element
            if (this.cursor) {
                this.cursor.style.display = 'none';
            }
            
            const el = document.elementFromPoint(this.x, this.y);
            
            if (this.cursor) {
                this.cursor.style.display = '';
            }
            
            // Only handle SELECT and text INPUT specially - everything else gets a physical click
            if (el && el.tagName === 'SELECT') {
                // SELECT elements need to be focused, not clicked
                el.focus();
                console.log('[Brow6el] MOUSE_EMU_FOCUS:SELECT::true:false:false');
                this.flashClick();
                return;
            }
            
            if (el && el.tagName === 'INPUT') {
                const type = (el.type || '').toLowerCase();
                const textInputTypes = ['text', 'password', 'email', 'search', 'tel', 'url', 'number', 'date', 'time', 'datetime-local', 'month', 'week'];
                
                if (textInputTypes.includes(type)) {
                    // Text input - focus it
                    el.focus();
                    console.log('[Brow6el] MOUSE_EMU_FOCUS:INPUT:' + type + ':false:true:false');
                    this.flashClick();
                    return;
                }
                // For checkbox, radio, button, submit, etc. - fall through to physical click
            }
            
            if (el && el.tagName === 'TEXTAREA') {
                // Textarea - focus it  
                el.focus();
                console.log('[Brow6el] MOUSE_EMU_FOCUS:TEXTAREA::false:true:false');
                this.flashClick();
                return;
            }
            
            // For everything else, send a physical click via C++ CEF events
            // Don't send element info - just let CEF click at the coordinates
            console.log('[Brow6el] MOUSE_EMU_CLICK:::false:false:false');
            this.flashClick();
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
                const type = el.type || '';
                return {
                    element: el,
                    tagName: el.tagName,
                    type: type,
                    isSelect: el.tagName === 'SELECT',
                    isInput: el.tagName === 'INPUT' || el.tagName === 'TEXTAREA',
                    isCheckboxOrRadio: type === 'checkbox' || type === 'radio'
                };
            }
            return null;
        },
        
        // Handle keys
        handleKey: function(key) {
            // If in grid mode, handle grid selection keys first
            if (this.gridMode) {
                // Check if key is a grid label (from config)
                const gridKeys = (window.__brow6el_grid_keys || 'qweasdzxc').toLowerCase();
                if (gridKeys.includes(key.toLowerCase())) {
                    if (this.jumpToGrid(key.toLowerCase())) {
                        return true;
                    }
                }
                // ESC to exit grid mode only (don't exit mouse emu)
                if (key === 'Escape') {
                    this.hideGrid();
                    // Reset zoom level and history when exiting grid mode
                    this.gridBaseX = 0;
                    this.gridBaseY = 0;
                    this.gridBaseWidth = 0;
                    this.gridBaseHeight = 0;
                    this.gridZoomHistory = [];
                    console.log('[Brow6el] MOUSE_EMU_GRID_HANDLED');
                    return true;
                }
                // Backspace to go back to parent grid
                if (key === 'Backspace') {
                    if (this.gridZoomHistory.length > 0) {
                        // Pop previous grid state from history
                        const prevState = this.gridZoomHistory.pop();
                        
                        this.hideGrid();
                        
                        this.gridBaseX = prevState.x;
                        this.gridBaseY = prevState.y;
                        this.gridBaseWidth = prevState.width;
                        this.gridBaseHeight = prevState.height;
                        
                        this.showGrid();
                    } else {
                        // Already at top level, just reset
                        this.resetGrid();
                    }
                    console.log('[Brow6el] MOUSE_EMU_GRID_HANDLED');
                    return true;
                }
                
                // In grid mode, hjkl/wasd should NOT work (only grid selection keys)
                if (key === 'w' || key === 'a' || key === 's' || key === 'd' ||
                    key === 'W' || key === 'A' || key === 'S' || key === 'D') {
                    // Ignore movement keys in grid mode
                    return true;
                }
            }
            
            switch(key) {
                // Grid mode toggle
                case 'g':
                case 'G':
                    if (this.gridMode) {
                        this.hideGrid();
                    } else {
                        this.showGrid();
                    }
                    return true;
                    
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
            // Cleanup grid mode
            this.hideGrid();
            // Cleanup inspect mode elements if active
            if (this.inspectInfoBox) {
                this.inspectInfoBox.remove();
                this.inspectInfoBox = null;
            }
            if (this.inspectHighlight) {
                this.inspectHighlight.remove();
                this.inspectHighlight = null;
            }
            // Remove inspect container
            const inspectContainer = document.getElementById('__brow6el_inspect_container');
            if (inspectContainer) {
                inspectContainer.remove();
            }
            this.inspectMode = false;
            this.lastInspectedElement = null;
            
            // Disconnect MutationObserver to stop watching DOM changes
            if (window.__brow6el_mutation_observer) {
                window.__brow6el_mutation_observer.disconnect();
                window.__brow6el_mutation_observer = null;
            }
            
            console.log('[Brow6el] MOUSE_EMU_CLOSED');
        }
    };
    
    window.__brow6el_mouse_emu = mouseEmu;
    mouseEmu.show();
    // Show grid by default when entering mouse emulation mode
    mouseEmu.showGrid();
    
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
