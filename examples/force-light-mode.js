// ==UserScript==
// @name         Force Light Mode
// @description  Prevents dark backgrounds and forces light color scheme
// @match        *://*/*
// ==/UserScript==

(function() {
    'use strict';
    
    // Remove any existing forced styles
    const existingStyle = document.getElementById('brow6el-force-light');
    if (existingStyle) {
        existingStyle.remove();
        return; // Toggle off
    }
    
    // Create style element to force light colors
    const style = document.createElement('style');
    style.id = 'brow6el-force-light';
    style.textContent = `
        /* Force light color scheme preference */
        :root {
            color-scheme: light !important;
        }
        
        /* Override common dark mode classes */
        body, html {
            background-color: white !important;
            color: black !important;
        }
        
        /* Prevent dark backgrounds on common containers */
        div, section, article, main, header, footer, nav, aside {
            background-color: transparent !important;
        }
        
        /* Ensure text is readable */
        p, span, a, li, td, th, h1, h2, h3, h4, h5, h6 {
            color: inherit !important;
        }
        
        /* Fix links */
        a {
            color: #0066cc !important;
        }
        
        a:visited {
            color: #551a8b !important;
        }
    `;
    
    document.head.appendChild(style);
    
    // Force reflow
    document.body.style.display = 'none';
    document.body.offsetHeight;
    document.body.style.display = '';
    
})();
