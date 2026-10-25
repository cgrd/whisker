#!/bin/bash
# SPDX-License-Identifier: MIT
set -e
mkdir -p ~/.local/share/pedal-stt/models
cd ~/.local/share/pedal-stt/models
curl -L -O https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/parakeet-unified-en-0.6b-Q8_0.gguf
echo "Done: $(ls -lh parakeet-unified-en-0.6b-Q8_0.gguf)"
