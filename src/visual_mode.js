// Visual mode for text selection
(function() {
    // Skip if in a frameset without content
    if (window.frames.length > 0 && document.body && document.body.children.length === 0) {
        return;
    }
    
    // Remove existing visual mode if any
    if (window.__brow6el_visual_mode) {
        window.__brow6el_visual_mode.cleanup();
    }
    
    const visualMode = {
        active: false,
        startNode: null,
        startOffset: 0,
        selectingMode: false, // false = caret positioning, true = extending selection
        caretDirection: 'forward', // Track which direction we selected for visual caret
        
        // Find first visible text node in viewport
        findVisibleTextNode: function() {
            // Get scroll position and viewport
            const scrollTop = window.pageYOffset || document.documentElement.scrollTop;
            const scrollBottom = scrollTop + window.innerHeight;
            
            const walker = document.createTreeWalker(
                document.body,
                NodeFilter.SHOW_TEXT,
                {
                    acceptNode: function(node) {
                        // Skip empty text nodes
                        if (!node.textContent.trim()) {
                            return NodeFilter.FILTER_REJECT;
                        }
                        
                        // Check if node is in viewport using page coordinates
                        const range = document.createRange();
                        range.selectNodeContents(node);
                        const rect = range.getBoundingClientRect();
                        const nodeTop = rect.top + scrollTop;
                        const nodeBottom = rect.bottom + scrollTop;
                        
                        // Node is visible if it's in the current viewport
                        if (nodeBottom > scrollTop && nodeTop < scrollBottom) {
                            return NodeFilter.FILTER_ACCEPT;
                        }
                        return NodeFilter.FILTER_SKIP;
                    }
                },
                false
            );
            
            return walker.nextNode();
        },
        
        // Initialize visual mode - start with collapsed cursor for positioning
        init: function() {
            this.active = true;
            this.selectingMode = false;
            
            const sel = window.getSelection();
            sel.removeAllRanges();
            
            let startTextNode = null;
            let startTextOffset = 0;
            
            // Try to use mouse cursor position if available
            if (window.__brow6el_mouse_emu && window.__brow6el_mouse_emu.x !== undefined) {
                const x = window.__brow6el_mouse_emu.x;
                const y = window.__brow6el_mouse_emu.y;
                
                // Hide mouse cursor temporarily
                const mouseCursor = window.__brow6el_mouse_emu.cursor;
                if (mouseCursor) mouseCursor.style.display = 'none';
                
                // Get text position at cursor
                if (document.caretRangeFromPoint) {
                    const range = document.caretRangeFromPoint(x, y);
                    if (range && range.startContainer.nodeType === Node.TEXT_NODE) {
                        startTextNode = range.startContainer;
                        startTextOffset = range.startOffset;
                    }
                } else if (document.caretPositionFromPoint) {
                    const pos = document.caretPositionFromPoint(x, y);
                    if (pos && pos.offsetNode.nodeType === Node.TEXT_NODE) {
                        startTextNode = pos.offsetNode;
                        startTextOffset = pos.offset;
                    }
                }
                
                if (mouseCursor) mouseCursor.style.display = '';
            }
            
            // Fallback: find first visible text node
            if (!startTextNode) {
                startTextNode = this.findVisibleTextNode();
                if (!startTextNode) {
                    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
                    startTextNode = walker.nextNode();
                }
            }
            
            if (startTextNode) {
                this.startNode = startTextNode;
                this.startOffset = startTextOffset;
                
                const range = document.createRange();
                range.setStart(startTextNode, startTextOffset);
                range.collapse(true);
                sel.addRange(range);
                
                this.updateCaretVisual();
            }
        },
        
        // Update visual caret indicator
        updateCaretVisual: function() {
            if (this.selectingMode) return;
            
            const sel = window.getSelection();
            if (!sel.rangeCount) return;
            
            const range = sel.getRangeAt(0);
            if (!range.collapsed) return;
            
            const startContainer = range.startContainer;
            const startOffset = range.startOffset;
            
            if (startContainer.nodeType === Node.TEXT_NODE) {
                const textLength = startContainer.length;
                if (startOffset < textLength) {
                    // Select one character forward
                    const newRange = document.createRange();
                    newRange.setStart(startContainer, startOffset);
                    newRange.setEnd(startContainer, startOffset + 1);
                    sel.removeAllRanges();
                    sel.addRange(newRange);
                    this.caretDirection = 'forward';
                } else if (startOffset > 0) {
                    // At end of text, select one character backward
                    const newRange = document.createRange();
                    newRange.setStart(startContainer, startOffset - 1);
                    newRange.setEnd(startContainer, startOffset);
                    sel.removeAllRanges();
                    sel.addRange(newRange);
                    this.caretDirection = 'backward';
                }
            }
            
            // Ensure CSS style exists
            if (!document.getElementById('brow6el-caret-style')) {
                const style = document.createElement('style');
                style.id = 'brow6el-caret-style';
                style.textContent = `
                    ::selection {
                        background: red !important;
                        color: white !important;
                    }
                `;
                document.head.appendChild(style);
            }
        },
        
        // Move caret/selection by character
        moveByChar: function(forward) {
            if (!this.active) return;
            
            const sel = window.getSelection();
            if (!sel.rangeCount) return;
            
            if (this.selectingMode) {
                sel.modify('extend', forward ? 'forward' : 'backward', 'character');
            } else {
                const range = sel.getRangeAt(0);
                range.collapse(this.caretDirection === 'forward');
                sel.removeAllRanges();
                sel.addRange(range);
                sel.modify('move', forward ? 'forward' : 'backward', 'character');
                setTimeout(() => this.updateCaretVisual(), 0);
            }
        },
        
        // Move caret/selection by word
        moveByWord: function(forward) {
            if (!this.active) return;
            
            const sel = window.getSelection();
            if (!sel.rangeCount) return;
            
            if (this.selectingMode) {
                sel.modify('extend', forward ? 'forward' : 'backward', 'word');
            } else {
                const range = sel.getRangeAt(0);
                range.collapse(this.caretDirection === 'forward');
                sel.removeAllRanges();
                sel.addRange(range);
                sel.modify('move', forward ? 'forward' : 'backward', 'word');
                setTimeout(() => this.updateCaretVisual(), 0);
            }
        },
        
        // Move caret/selection by line
        moveByLine: function(down) {
            if (!this.active) return;
            
            const sel = window.getSelection();
            if (!sel.rangeCount) return;
            
            if (this.selectingMode) {
                sel.modify('extend', down ? 'forward' : 'backward', 'line');
            } else {
                const range = sel.getRangeAt(0);
                range.collapse(this.caretDirection === 'forward');
                sel.removeAllRanges();
                sel.addRange(range);
                sel.modify('move', down ? 'forward' : 'backward', 'line');
                setTimeout(() => this.updateCaretVisual(), 0);
            }
        },
        
        // Toggle between caret mode and selecting mode
        startSelecting: function() {
            if (this.selectingMode) return; // Already selecting
            
            this.selectingMode = true;
            
            // Change selection color to yellow for selection mode
            const style = document.getElementById('brow6el-caret-style');
            if (style) {
                style.textContent = `
                    ::selection {
                        background: yellow !important;
                        color: black !important;
                    }
                `;
            }
            
            // Ensure we have a collapsed selection at current position
            // This sets the anchor point for future extends
            const sel = window.getSelection();
            if (sel.rangeCount > 0) {
                const range = sel.getRangeAt(0);
                range.collapse(true);
                // Re-set the range to ensure anchor is properly set
                sel.removeAllRanges();
                sel.addRange(range);
                
                this.startNode = range.startContainer;
                this.startOffset = range.startOffset;
                
                // Select one character to make the selection visible
                if (range.startContainer.nodeType === Node.TEXT_NODE) {
                    const textLength = range.startContainer.length;
                    if (range.startOffset < textLength) {
                        range.setEnd(range.startContainer, range.startOffset + 1);
                        sel.removeAllRanges();
                        sel.addRange(range);
                    }
                }
            }
        },
        
        // Get selected text
        getSelectedText: function() {
            const sel = window.getSelection();
            return sel.toString();
        },
        
        // Copy selection to clipboard
        copySelection: function() {
            const text = this.getSelectedText();
            if (text) {
                console.log('[Brow6el] VISUAL_MODE_COPY:' + text);
            }
        },
        
        // Clear selection and exit
        cleanup: function() {
            this.active = false;
            this.selectingMode = false;
            
            // Remove style
            const style = document.getElementById('brow6el-caret-style');
            if (style) {
                style.remove();
            }
            
            const sel = window.getSelection();
            sel.removeAllRanges();
            console.log('[Brow6el] VISUAL_MODE_CLOSED');
        },
        
        // Handle key press
        handleKey: function(key) {
            if (!this.active) return false;
            
            switch(key) {
                case 'v':
                    // Toggle from caret mode to selecting mode
                    this.startSelecting();
                    return true;
                case 'h':
                    this.moveByChar(false);
                    return true;
                case 'l':
                    this.moveByChar(true);
                    return true;
                case 'b':
                    this.moveByWord(false);
                    return true;
                case 'w':
                    this.moveByWord(true);
                    return true;
                case 'j':
                    this.moveByLine(true);
                    return true;
                case 'k':
                    this.moveByLine(false);
                    return true;
                case 'y':
                case 'Enter':
                    this.copySelection();
                    this.cleanup();
                    return true;
                case 'Escape':
                    this.cleanup();
                    return true;
            }
            return false;
        }
    };
    
    window.__brow6el_visual_mode = visualMode;
    visualMode.init();
})();
