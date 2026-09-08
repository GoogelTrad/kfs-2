To compile first you need to run : 
```docker build --no-cache -t kfs-builder .```

Then :
```docker run --rm -it -v $(pwd):/kfs kfs-builder make clean```
```docker run --rm -it -v $(pwd):/kfs kfs-builder make iso``` 

Or just : 
```docker run --rm -it -v $(pwd):/kfs kfs-builder make re```

And finnaly for the moment use qumu and run :
```qemu-system-i386 -cdrom mykernel.iso -display default,show-cursor=on```
