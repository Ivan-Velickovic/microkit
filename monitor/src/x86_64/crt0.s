	.section .text.start
	.globl _start
_start:
	leaq	0xff0 + _stack(%rip), %rsp
	call	main
