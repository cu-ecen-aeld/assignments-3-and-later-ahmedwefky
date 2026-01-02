The kernel Oops message is :


echo “hello_world” > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041bb6000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 205 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dbbd20
x29: ffffffc008dbbd80 x28: ffffff8001ba4240 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008dbbdc0
x20: 000000557e0d5a10 x19: ffffff8001b71500 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008dbbdc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---


The most critical line is:

pc : faulty_write+0x10/0x20 [faulty]

"faulty_write": This means that the crash occurred inside the function faulty_write.

"+0x10": This is the offset (in bytes) from the start of the function where the crash happened.

"Unable to handle kernel NULL pointer dereference": This means that the error was trying to access memory address 0.

To find the exact line number in the C file corresponding to faulty_write+0x10, GDB can be used on the compiled kernel module (faulty.ko) if the source file faulty.c was compiled with debug symbols. I tried to modify the makefile used to compile the misc-modules adding the -g flag but this resulted in stripped binaries again. Buildroot strips debug symbols from binaries before installing them even after disabling BR2_STRIP_strip flag.

I tried out the 2nd method mentioned in the lectures based on objdump and got the following output:

aarch64-none-linux-gnu-objdump -d faulty.ko 

faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop

This shows that the instruction stored at offset 0x10 of the function faulty_write is b900003f str wzr, [x1].

This means that the store instruction tries to write to a register that holds a NULL pointero or 0 as x1 contains 0 at the beginning of the function.

Since the source code of faulty.c is available, I looked at the faulty_write function directly.

ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}

The bug is clearly visible in the line: *(int *)0 = 0;

This line casts the integer 0 to a pointer (int *), and then attempts to dereference it (*) to write the value 0 to that memory address. 
Address 0 is a NULL pointer, and accessing it causes the fault seen in the Oops message.










aarch64-none-linux-gnu-objdump -d faulty.ko 

faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop
