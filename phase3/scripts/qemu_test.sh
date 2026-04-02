#!/bin/bash
cd ~/sel4-x86/jbuild
qemu-system-x86_64 -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce -nographic -serial mon:stdio -m 4G -kernel images/kernel-x86_64-pc99 -initrd images/sel4test-driver-image-x86_64-pc99
