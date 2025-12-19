#!/bin/bash

# Get the directory where this script is located
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Set library path to use local libcef.so
export LD_LIBRARY_PATH="$DIR:$LD_LIBRARY_PATH"

# Run the browser
"$DIR/brow6el" "$@"
