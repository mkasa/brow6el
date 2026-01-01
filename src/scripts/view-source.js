// View Source - Display HTML source code in a readable format
// Shows the complete HTML source of the current page with syntax highlighting
(function() {
    'use strict';
    
    // Check if source viewer already exists
    if (document.getElementById('brow6el-source-viewer')) {
        // Toggle off - remove viewer
        document.getElementById('brow6el-source-viewer').remove();
        return;
    }
    
    // Get the complete HTML source (serialized to preserve exact formatting)
    let htmlSource = new XMLSerializer().serializeToString(document);
    
    // Format/prettify HTML
    htmlSource = formatHTML(htmlSource);
    
    // Create overlay container
    const viewer = document.createElement('div');
    viewer.id = 'brow6el-source-viewer';
    viewer.tabIndex = 0; // Make focusable for keyboard navigation
    viewer.style.cssText = `
        position: fixed !important;
        top: 0 !important;
        left: 0 !important;
        width: 100% !important;
        height: 100% !important;
        background: rgba(0, 0, 0, 0.95) !important;
        z-index: 2147483647 !important;
        overflow: auto !important;
        font-family: 'Courier New', Consolas, monospace !important;
        font-size: 13px !important;
        color: #f8f8f2 !important;
        padding: 20px !important;
        box-sizing: border-box !important;
        line-height: 1.5 !important;
        outline: none !important;
    `;
    
    // Create header
    const header = document.createElement('div');
    header.style.cssText = `
        position: sticky !important;
        top: 0 !important;
        background: #1e1e1e !important;
        padding: 10px !important;
        margin: -20px -20px 20px -20px !important;
        border-bottom: 2px solid #0ff !important;
        font-size: 14px !important;
        font-weight: bold !important;
        color: #0ff !important;
        z-index: 1 !important;
    `;
    
    const title = document.createElement('span');
    title.textContent = 'View Source: ' + document.title;
    title.style.marginRight = '20px';
    
    const escHint = document.createElement('span');
    escHint.textContent = '(Press ESC to close)';
    escHint.style.cssText = `
        float: right !important;
        color: #888 !important;
        font-weight: normal !important;
        font-size: 12px !important;
    `;
    
    const stats = document.createElement('div');
    stats.style.cssText = `
        margin-top: 5px !important;
        font-size: 11px !important;
        color: #888 !important;
        font-weight: normal !important;
    `;
    const lines = htmlSource.split('\n').length;
    const chars = htmlSource.length;
    stats.textContent = `${lines} lines, ${chars.toLocaleString()} characters`;
    
    header.appendChild(title);
    header.appendChild(escHint);
    header.appendChild(stats);
    
    // Create source code container
    const sourceContainer = document.createElement('pre');
    sourceContainer.style.cssText = `
        margin: 0 !important;
        padding: 0 !important;
        white-space: pre-wrap !important;
        word-wrap: break-word !important;
        overflow-wrap: break-word !important;
        tab-size: 2 !important;
        flex: 1 !important;
        min-width: 0 !important;
    `;
    
    // Add CSS for syntax highlighting using custom tags (to avoid style= in output)
    const style = document.createElement('style');
    style.textContent = `
        #brow6el-source-viewer c1 { color: #608b4e; font-style: italic; }
        #brow6el-source-viewer c2 { color: #808080; font-weight: bold; }
        #brow6el-source-viewer c3 { color: #569cd6; }
        #brow6el-source-viewer c4 { color: #4ec9b0; }
        #brow6el-source-viewer c5 { color: #9cdcfe; }
        #brow6el-source-viewer c6 { color: #d4d4d4; }
        #brow6el-source-viewer c7 { color: #ce9178; }
    `;
    document.head.appendChild(style);
    
    // Apply syntax highlighting
    const highlighted = syntaxHighlight(htmlSource);
    sourceContainer.innerHTML = highlighted;
    
    // Add line numbers
    const lineNumbers = document.createElement('div');
    lineNumbers.style.cssText = `
        padding-right: 10px !important;
        margin-right: 10px !important;
        border-right: 1px solid #444 !important;
        color: #666 !important;
        text-align: right !important;
        user-select: none !important;
        min-width: 40px !important;
        flex-shrink: 0 !important;
    `;
    
    const lineCount = htmlSource.split('\n').length;
    for (let i = 1; i <= lineCount; i++) {
        const lineNum = document.createElement('div');
        lineNum.textContent = i;
        lineNumbers.appendChild(lineNum);
    }
    
    // Wrap source with line numbers
    const wrapper = document.createElement('div');
    wrapper.style.cssText = `
        display: flex !important;
        align-items: flex-start !important;
        background: #1e1e1e !important;
        padding: 15px !important;
        border-radius: 5px !important;
        overflow-x: auto !important;
        width: 100% !important;
        box-sizing: border-box !important;
    `;
    wrapper.appendChild(lineNumbers);
    wrapper.appendChild(sourceContainer);
    
    // Assemble viewer
    viewer.appendChild(header);
    viewer.appendChild(wrapper);
    document.body.appendChild(viewer);
    
    // Focus the viewer so hjkl/arrow keys work in STANDARD mode
    setTimeout(() => viewer.focus(), 100);
    
    // Close on ESC key
    const escHandler = (e) => {
        if (e.key === 'Escape') {
            viewer.remove();
            style.remove();
            document.removeEventListener('keydown', escHandler);
        }
    };
    document.addEventListener('keydown', escHandler);
    
    // Format HTML with indentation
    function formatHTML(html) {
        let formatted = '';
        let indent = 0;
        const tab = '  '; // 2 spaces
        
        // Split by tags
        const tokens = html.split(/(<[^>]+>)/g).filter(t => t.trim());
        
        for (let i = 0; i < tokens.length; i++) {
            const token = tokens[i];
            
            if (token.startsWith('</')) {
                // Closing tag - decrease indent before adding
                indent = Math.max(0, indent - 1);
                formatted += tab.repeat(indent) + token + '\n';
            } else if (token.startsWith('<')) {
                // Check if self-closing or special tags
                const isSelfClosing = token.endsWith('/>') || 
                    /^<(br|hr|img|input|meta|link|area|base|col|embed|param|source|track|wbr)[>\s]/i.test(token);
                const isComment = token.startsWith('<!--');
                const isDoctype = token.startsWith('<!DOCTYPE') || token.startsWith('<!doctype');
                
                formatted += tab.repeat(indent) + token + '\n';
                
                // Increase indent for opening tags (but not for self-closing)
                if (!isSelfClosing && !isComment && !isDoctype && !token.startsWith('</')) {
                    indent++;
                }
            } else {
                // Text content - only add if not just whitespace
                const trimmed = token.trim();
                if (trimmed) {
                    formatted += tab.repeat(indent) + trimmed + '\n';
                }
            }
        }
        
        return formatted.trim();
    }
    
    // Syntax highlighting using CSS classes instead of inline styles
    function syntaxHighlight(html) {
        // First escape HTML entities to prevent injection
        const escaped = html
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
        
        const lines = escaped.split('\n');
        const result = [];
        
        for (const line of lines) {
            let colored = line;
            
            // Highlight comments (must be first)
            colored = colored.replace(/(&lt;!--.*?--&gt;)/g, 
                '<c1>$1</c1>');
            
            // Highlight DOCTYPE
            colored = colored.replace(/(&lt;!DOCTYPE[^&]*&gt;)/gi, 
                '<c2>$1</c2>');
            
            // Highlight opening/closing tags
            colored = colored.replace(/(&lt;\/?)([\w-:]+)/g, 
                '<c3>$1</c3><c4>$2</c4>');
            
            // Highlight attributes (word before =)
            colored = colored.replace(/(\s)([\w-:]+)(=)/g, 
                '$1<c5>$2</c5><c6>$3</c6>');
            
            // Highlight attribute values (quoted strings after =)
            colored = colored.replace(/=(&quot;[^&]*&quot;)/g, 
                '=<c7>$1</c7>');
            
            // Highlight closing bracket
            colored = colored.replace(/(\s*)(\/?&gt;)/g, 
                '$1<c3>$2</c3>');
            
            result.push(colored);
        }
        
        return result.join('\n');
    }
    
    console.log('[Brow6el] View Source activated - ' + lines + ' lines');
})();
