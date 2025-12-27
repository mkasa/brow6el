// Simple AdBlock for brow6el
// Blocks common ad elements and tracking scripts

(function() {
    'use strict';
    
    // Common ad-related selectors
    const adSelectors = [
        // Generic ad containers
        '[id*="ad-"]',
        '[id*="ads-"]',
        '[id*="_ad_"]',
        '[id*="google_ads"]',
        '[id*="sponsor"]',
        '[class*="ad-"]',
        '[class*="ads-"]',
        '[class*="_ad_"]',
        '[class*="advertisement"]',
        '[class*="sponsor"]',
        '[class*="banner"]',
        
        // Common ad networks and widgets
        'iframe[src*="doubleclick.net"]',
        'iframe[src*="googlesyndication.com"]',
        'iframe[src*="googleadservices.com"]',
        'iframe[src*="adservice.google"]',
        'iframe[src*="facebook.com/plugins"]',
        'iframe[src*="ads-twitter.com"]',
        
        // Social media widgets
        '.fb-like',
        '.fb-share-button',
        '.twitter-share-button',
        
        // Cookie consent banners (optional - uncomment if you want to block these)
        // '[class*="cookie-banner"]',
        // '[class*="cookie-consent"]',
        // '[id*="cookie-notice"]'
    ];
    
    // Remove ad elements
    function removeAds() {
        let removed = 0;
        
        adSelectors.forEach(selector => {
            try {
                const elements = document.querySelectorAll(selector);
                elements.forEach(el => {
                    // Skip if already removed
                    if (!el.parentNode) return;
                    
                    // Hide element
                    el.style.display = 'none';
                    el.style.visibility = 'hidden';
                    el.style.opacity = '0';
                    el.style.height = '0';
                    el.style.width = '0';
                    el.style.position = 'absolute';
                    el.style.left = '-9999px';
                    
                    // Remove from DOM
                    el.remove();
                    removed++;
                });
            } catch (e) {
                // Ignore selector errors
            }
        });
        
        return removed;
    }
    
    // Block ad scripts from loading
    function blockAdScripts() {
        const adDomains = [
            'doubleclick.net',
            'googlesyndication.com',
            'googleadservices.com',
            'google-analytics.com',
            'googletagmanager.com',
            'facebook.net',
            'connect.facebook.net',
            'scorecardresearch.com',
            'outbrain.com',
            'taboola.com',
            'adnxs.com',
            'adsrvr.org',
            'advertising.com'
        ];
        
        // Block scripts
        document.addEventListener('beforeload', function(e) {
            if (e.target.tagName === 'SCRIPT' || e.target.tagName === 'IFRAME') {
                const src = e.target.src || '';
                for (const domain of adDomains) {
                    if (src.includes(domain)) {
                        e.preventDefault();
                        e.stopPropagation();
                        return false;
                    }
                }
            }
        }, true);
    }
    
    // Clean up spacing after removing ads
    function cleanupSpacing() {
        // Remove empty containers that held ads
        const containers = document.querySelectorAll('div, aside, section');
        containers.forEach(container => {
            if (container.children.length === 0 && 
                container.textContent.trim() === '' &&
                container.offsetHeight > 0) {
                container.style.display = 'none';
            }
        });
    }
    
    // Initialize adblock
    function init() {
        const removed = removeAds();
        if (removed > 0) {
            console.log(`[AdBlock] Blocked ${removed} ad elements`);
        }
        cleanupSpacing();
    }
    
    // Run on page load
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
    
    // Watch for dynamically added ads
    const observer = new MutationObserver((mutations) => {
        let needsCheck = false;
        mutations.forEach((mutation) => {
            if (mutation.addedNodes.length > 0) {
                needsCheck = true;
            }
        });
        if (needsCheck) {
            removeAds();
        }
    });
    
    observer.observe(document.body, {
        childList: true,
        subtree: true
    });
    
    // Try to block scripts
    try {
        blockAdScripts();
    } catch (e) {
        // beforeload event not supported in all browsers
    }
    
    console.log('[AdBlock] Initialized');
})();
