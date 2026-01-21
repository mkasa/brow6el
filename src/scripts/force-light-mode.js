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
        
        /* Override body-level dark backgrounds only */
        html {
            background-color: white !important;
            color: black !important;
        }
        
        body {
            background-color: white !important;
            color: black !important;
        }
        
        /* Don't override container backgrounds - let site styles work */
        /* Only target dark backgrounds specifically */
        [style*="background-color: rgb(0, 0, 0)"],
        [style*="background-color: black"],
        [style*="background: rgb(0, 0, 0)"],
        [style*="background: black"] {
            background-color: white !important;
        }
        
        /* Fix text colors only if they're white/light on dark */
        [style*="color: rgb(255, 255, 255)"],
        [style*="color: white"] {
            color: black !important;
        }
        
        /* Override dark mode media query at CSS level */
        @media (prefers-color-scheme: dark) {
            :root {
                color-scheme: light !important;
            }
        }
    `;
    
    document.head.appendChild(style);
    
    // Override matchMedia to report light mode
    const originalMatchMedia = window.matchMedia;
    window.matchMedia = function(query) {
        if (query && query.includes('prefers-color-scheme')) {
            return {
                matches: query.includes('light'),
                media: query,
                onchange: null,
                addListener: function() {},
                removeListener: function() {},
                addEventListener: function() {},
                removeEventListener: function() {},
                dispatchEvent: function() { return true; }
            };
        }
        return originalMatchMedia.call(this, query);
    };
    
})();
