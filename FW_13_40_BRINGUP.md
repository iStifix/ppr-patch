# Firmware 13.40 bring-up

This branch does **not** add a 13.40 patch profile yet. The current profile
table ends at 11.40. The patcher deliberately refuses an unknown A53 image.
Do not reuse an 11.x address or instruction word on 13.40.

The first on-console step is the read-only `build/a53_ppr_probe.elf`. It uses
the existing GET_CONF transport to report the system firmware, embedded A53
release string and target type, then exits before profile lookup, fast
transport enablement or any A53 write. Its output establishes which exact
retail/testkit/devkit image must be analyzed.

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
