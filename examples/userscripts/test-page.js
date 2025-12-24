// Auto-inject script for test_dialogs.html
(function() {
    'use strict';
    
    console.log('[UserScript] Test page customization loaded');
    
    // Add a colorful banner at the top
    const banner = document.createElement('div');
    banner.style.cssText = 'background: linear-gradient(90deg, #667eea 0%, #764ba2 100%); color: white; padding: 15px; text-align: center; font-weight: bold; font-size: 18px; margin-bottom: 10px;';
    banner.textContent = '🚀 User Script Auto-Injected on Test Page!';
    
    if (document.body.firstChild) {
        document.body.insertBefore(banner, document.body.firstChild);
    } else {
        document.body.appendChild(banner);
    }
    
    console.log('[UserScript] Test page customization complete');
})();
