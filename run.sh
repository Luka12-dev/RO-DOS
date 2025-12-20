make clean # comment this line if you don't want to clean the build
make all   # Build all files (.asm and .c into .o then .bin then .img or .iso)
make run   # this will run as .img

# make run-iso # uncomment this line if you want to run as .iso not .img

# make clean - clean the previus builds
# make all - build all the .asm and .c files
# make run - run a QEMU
# WARNING: For building RO-DOS i recommend to use Linux, specify Ubuntu.
# If you are on Windows use WSL for best compatibility, use something like Ubuntu 24.04 (i recommend)
echo RO-DOS is shutdown.