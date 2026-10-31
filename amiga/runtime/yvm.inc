| VM context (a6) layout shared by the generated code, the assembler glue and the C runtime
	.set vm_cc, 0          | byte: E F H I bits of the 6809 CC (bits 7,6,5,4); N Z V C live in the 68k CCR
	.set vm_dp, 1          | byte: DP register
