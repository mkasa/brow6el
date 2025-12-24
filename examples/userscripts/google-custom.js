// Example user script for Google
(function() {
    'use strict';
    
    console.log('[UserScript] Google customization script loaded');
    
    // Change background color
    if (document.body) {
        document.body.style.backgroundColor = '#f0f8ff';
    }
    
    // Add custom message at top of page
    const bannerElement = document.querySelector('div[role="banner"]');
    if (bannerElement) {
        const customMsg = document.createElement('div');
        customMsg.style.cssText = 'background: #4CAF50; color: white; padding: 10px; text-align: center; font-weight: bold; z-index: 9999;';
        customMsg.textContent = '✨ Custom script loaded by brow6el!';
        bannerElement.parentNode.insertBefore(customMsg, bannerElement);
    } else {
        // Fallback: add to top of body
        const customMsg = document.createElement('div');
        customMsg.style.cssText = 'background: #4CAF50; color: white; padding: 10px; text-align: center; font-weight: bold; position: relative; z-index: 9999;';
        customMsg.textContent = '✨ Custom script loaded by brow6el!';
        if (document.body.firstChild) {
            document.body.insertBefore(customMsg, document.body.firstChild);
        } else {
            document.body.appendChild(customMsg);
        }
    }
    
    console.log('[UserScript] Google customization complete');
})();
