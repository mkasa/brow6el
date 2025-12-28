// Auto-redirect from framesets to the main frame content
// Handles nested framesets by redirecting recursively

(function() {
    'use strict';
    
    // Prevent infinite redirect loops - max 5 redirects
    const MAX_REDIRECTS = 5;
    const REDIRECT_COUNT_KEY = '_brow6el_frameset_redirects';
    
    let redirectCount = parseInt(sessionStorage.getItem(REDIRECT_COUNT_KEY) || '0');
    
    console.log('[Frameset Redirect] Script loaded, redirect count:', redirectCount);
    
    if (redirectCount >= MAX_REDIRECTS) {
        console.log('[Frameset Redirect] Max redirects reached, stopping');
        sessionStorage.removeItem(REDIRECT_COUNT_KEY);
        return;
    }
    
    // Better frameset detection
    const hasFrameset = document.querySelector('frameset') !== null;
    const hasFrames = window.frames.length > 0;
    
    console.log('[Frameset Redirect] Has frameset element:', hasFrameset);
    console.log('[Frameset Redirect] Has frames:', hasFrames);
    
    if (hasFrameset || (hasFrames && !document.body)) {
        console.log('[Frameset Redirect] Detected frameset document');
        
        // Try to find the main frame
        let mainFrameUrl = null;
        
        // Look for frame elements
        const frameElements = document.querySelectorAll('frame, iframe');
        console.log('[Frameset Redirect] Found', frameElements.length, 'frame elements');
        
        if (frameElements.length > 0) {
            // Take the first frame with a src
            for (const frame of frameElements) {
                if (frame.src && frame.src !== 'about:blank') {
                    mainFrameUrl = frame.src;
                    console.log('[Frameset Redirect] Found frame src:', mainFrameUrl);
                    break;
                }
            }
        }
        
        // If no frame src found, try accessing frame location
        if (!mainFrameUrl && window.frames.length > 0) {
            try {
                mainFrameUrl = window.frames[0].location.href;
                console.log('[Frameset Redirect] Got frame location:', mainFrameUrl);
            } catch (e) {
                console.log('[Frameset Redirect] Cannot access frame location (cross-origin):', e.message);
            }
        }
        
        // Redirect to the main frame content
        if (mainFrameUrl && mainFrameUrl !== window.location.href && mainFrameUrl !== 'about:blank') {
            redirectCount++;
            sessionStorage.setItem(REDIRECT_COUNT_KEY, redirectCount.toString());
            console.log('[Frameset Redirect] Redirecting to:', mainFrameUrl, '(attempt', redirectCount + ')');
            window.location.replace(mainFrameUrl);
        } else {
            console.log('[Frameset Redirect] Could not determine valid frame URL');
            sessionStorage.removeItem(REDIRECT_COUNT_KEY);
        }
    } else {
        console.log('[Frameset Redirect] Not a frameset, clearing redirect count');
        sessionStorage.removeItem(REDIRECT_COUNT_KEY);
    }
})();
