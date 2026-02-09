#!/bin/bash

# RO-DOS Build & Run Script

# Clean previous build artifacts (Optional)
# make clean 

# Build the system
echo "Building RO-DOS..."
make all

# Run in QEMU (defaulting to HDD image)
echo "Starting RO-DOS in QEMU..."
make run

# Alternative: Run as ISO
# make run-iso 

# Information
# make clean    - Clean build artifacts
# make all      - Compile assembly and C source files
# make run      - Launch QEMU with disk image

echo "---------------------------------------------------"
echo "WARNING: For building RO-DOS, Linux (Ubuntu) is recommended."
echo "Windows users should use WSL (Ubuntu 24.04 recommended)."
echo "---------------------------------------------------"

echo "RO-DOS has shut down."