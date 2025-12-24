# User Scripts for brow6el

Custom JavaScript injection system for brow6el browser.

## Quick Start

1. Create scripts directory:
```bash
mkdir -p ~/.brow6el/userscripts
```

2. Add your `.js` files to `~/.brow6el/userscripts/`

3. Configure in `~/.brow6el/userscripts.conf`

## Keyboard Shortcuts

- **Ctrl+U** - Open user scripts menu (select and inject manually)
- **Ctrl+Y** - Toggle auto-inject on/off
- In menu: ↑/↓ to navigate, Enter to inject, ESC to close

## Configuration File Format

`~/.brow6el/userscripts.conf`:

```
# Global settings
auto_inject=true

# Script entries
filename|display_name|enabled|url_pattern1,url_pattern2,...
```

### Example Configuration

```
auto_inject=true

google-custom.js|Google Custom|true|*google.com*,*google.co.*
dark-mode.js|Dark Mode|false|
amazon-helper.js|Amazon Helper|true|*amazon.com/*,*amazon.co.uk/*
github-tweaks.js|GitHub Tweaks|true|*github.com/*
```

## URL Pattern Matching

Supports wildcards:
- `*` - Matches any characters
- `?` - Matches single character

Examples:
- `*google.com*` - Matches any Google domain
- `https://example.com/*` - Matches all pages on example.com
- `*://*.wikipedia.org/*` - Matches all Wikipedia sites
- `file://*test*.html` - Matches local test HTML files

## Example Scripts

### Dark Mode (dark-mode.js)
```javascript
// Invert colors for dark mode
console.log('[UserScript] Dark mode activated');
document.documentElement.style.filter = 'invert(1) hue-rotate(180deg)';

// Fix images and videos
const media = document.querySelectorAll('img, video, picture');
media.forEach(el => {
    el.style.filter = 'invert(1) hue-rotate(180deg)';
});
```

### Custom Styles (custom-style.js)
```javascript
// Add custom CSS
const style = document.createElement('style');
style.textContent = `
    body { font-family: 'Arial', sans-serif !important; }
    a { color: #0066cc !important; }
`;
document.head.appendChild(style);
```

### Remove Ads (ad-blocker.js)
```javascript
// Simple ad blocker
const selectors = [
    '.ad', '.advertisement', '[id*="ad-"]',
    '[class*="sponsor"]', 'iframe[src*="ads"]'
];
selectors.forEach(sel => {
    document.querySelectorAll(sel).forEach(el => el.remove());
});
```

### Auto-fill Forms (form-helper.js)
```javascript
// Auto-fill common form fields
document.querySelectorAll('input[name="email"]').forEach(input => {
    input.value = 'your@email.com';
});
```

## Features

- **Auto-injection**: Scripts automatically inject when URL matches pattern
- **Manual injection**: Press Ctrl+U to select and inject any script
- **Enable/disable**: Control which scripts are active
- **Pattern matching**: Flexible URL pattern system
- **No restart needed**: Changes to scripts take effect on next page load

## Tips

1. **Test scripts**: Use Ctrl+U to manually test before adding URL patterns
2. **Console logging**: Use `console.log()` to debug your scripts (Ctrl+K to view)
3. **Script order**: Scripts inject in the order they appear in config
4. **Reload page**: Changes to scripts require page reload (Ctrl+R)

## Disable Auto-injection

Set in config file:
```
auto_inject=false
```

Or leave URL patterns empty for manual-only scripts.

## Troubleshooting

**Script not loading?**
- Check file permissions: `chmod 644 ~/.brow6el/userscripts/*.js`
- Check syntax: Test in browser console first
- Check config format: Verify pipe `|` separators

**Pattern not matching?**
- Add debug: `console.log(window.location.href)` to see exact URL
- Try broader pattern: `*example.com*` instead of `https://example.com/`

**Script errors?**
- Check console: Ctrl+K
- Verify JavaScript syntax
- Test on simpler page first
