# Firmware 13.40 bring-up

This branch does **not** add a 13.40 patch profile yet. The current profile
table ends at 11.40. The patcher deliberately refuses an unknown A53 image.
Do not reuse an 11.x address or instruction word on 13.40.

The first on-console step is the read-only `build/a53_ppr_probe.elf`. It uses
the existing GET_CONF transport to report the system firmware, embedded A53
release string and target type, then exits before profile lookup, fast
transport enablement or any A53 write. Its output establishes which exact
retail/testkit/devkit image must be analyzed.

On a retail 13.40 console, GET_CONF returned
`cex(1.1.2.01):Oberon-KDE 2026/05/28 02:23 releases/13.40 228687`.
The old `PPR_PATCH_LAYOUT_PA` (`0x887f0000`) returned SDBGP read status
`0x20` with zero transferred bytes, even for a 4-byte read. This is an
address/layout change, not a general READ_MEMORY failure: low A53 SRAM
addresses through `0x8ffff` were readable. The separate
`build/a53_ppr_dump_sram.elf` produced a complete 0x90000-byte read-only
snapshot from the owner's console. That snapshot is intentionally ignored by
Git and is not part of this repository.

The SRAM snapshot contains AArch64 code and late-ABI function prologues, but
the complete old helper and terminal SHA signatures do not match. Do not
promote any partial signature match to a patch site. The mapped DEV/G6 image
and the new runtime layout still need exact extraction and independent
verification.

To produce a 13.40 profile, obtain the matching full A53 ELF or `mp4_dram`
image from the operator's own console/firmware. Keep the image and any live
memory dump outside this repository. Record its SHA-256 and embedded release
marker. Only then extend `generate_ppr_profiles.py` and regenerate
`ppr_profiles.inc`; run the independent verifier against the same image and
confirm all caller sites, stock instruction words, executable caves, wrapper
bytes and terminal notification semantics. A status-only live readback must
match the candidate profile before an install is considered.

This A53 selector handles only marked PPR package reads. Complete fPKG on
13.40 also requires a verified kernel-side mount path to set the exact
`AES=0xff, SHA=0xfe` sentinel for plaintext mounts. The supplied Kstuff NG
1.00 ELF contains PPR-related symbols, but its behavior on 13.40 has not yet
been established. Treat it as a separate component until its source or a
read-only trace confirms marker publication and mount behavior.

Validation order: read-only probe; exact offline profile generation and
verification; read-only status on a fresh console; native package mount;
operator-confirmed idle A53 install; one test fPKG; then ExtFs/inner PFS,
APR and NSID2 paths. Never run an installer merely to test whether a profile
exists.
