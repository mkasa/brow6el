// Dark mode for any page
console.log('[UserScript] Dark mode activated');

// Invert colors
document.documentElement.style.filter = 'invert(1) hue-rotate(180deg)';

// Fix images and videos
const mediaElements = document.querySelectorAll('img, video, picture, [style*="background-image"]');
mediaElements.forEach(el => {
    el.style.filter = 'invert(1) hue-rotate(180deg)';
});
