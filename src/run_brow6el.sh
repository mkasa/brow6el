#!/bin/bash

# Get the directory where this script is located
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Set library path to use local libcef.so
export LD_LIBRARY_PATH="$DIR:$LD_LIBRARY_PATH"

# Set Vulkan ICD path to use local SwiftShader
export VK_ICD_FILENAMES="$DIR/vk_swiftshader_icd.json"

# Run the browser
"$DIR/brow6el" "$@"
