# Brow6el Examples

This directory contains example files to help you get started with brow6el.

## Contents

### userscripts/
Example user scripts that demonstrate the custom JavaScript injection system.

**To install:**
```bash
# Copy example scripts to your user scripts directory
mkdir -p ~/.brow6el/userscripts
cp examples/userscripts/*.js ~/.brow6el/userscripts/

# Copy example config (edit URL patterns as needed)
cp examples/userscripts/userscripts.conf.example ~/.brow6el/userscripts.conf
```

#### Example Scripts

**dark-mode.js**
- Inverts colors for a dark theme
- Manual injection only (no URL pattern)
- Press `Ctrl+U` and select "Dark Mode" to activate

**google-custom.js**
- Adds a green banner to Google pages
- Auto-injects on `*google.com*` and `*google.co.*`
- Changes background color to light blue

**test-page.js**
- Example script demonstrating auto-injection
- Useful for testing auto-injection patterns

**userscripts.conf.example**
- Example configuration file
- Shows how to configure auto-injection patterns
- Demonstrates wildcard URL matching

## Quick Start

1. **Install example user scripts:**
   ```bash
   mkdir -p ~/.brow6el/userscripts
   cp examples/userscripts/*.js ~/.brow6el/userscripts/
   cp examples/userscripts/userscripts.conf.example ~/.brow6el/userscripts.conf
   ```

2. **Test user scripts:**
   - Press `Ctrl+U` to manually inject dark-mode.js
   - Press `Ctrl+Y` to toggle auto-injection
   - Visit Google to see the custom script in action

## See Also

- [USERSCRIPTS.md](../USERSCRIPTS.md) - Complete user scripts documentation
- [README.md](../README.md) - Main documentation
