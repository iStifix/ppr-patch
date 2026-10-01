#include "a53_transport.h"
#include "notify.h"
#include "ppr_patch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include <ps5/kernel.h>

#define printf ppr_printf
#define puts ppr_puts

#define A53_LAYOUT_PA 0x887F0000ULL
#define A53_LAYOUT_HEADER_SIZE 0x148U
#define A53_LAYOUT_MAX_RECORDS 32U

struct a53_layout_record_probe {
    uint32_t id;
    uint32_t flags;
    uint64_t g6_base;
    uint64_t g6_size;
    uint64_t sram_base;
    uint64_t sram_size;
    uint64_t unknown_28;
    uint64_t mapped_base;
    uint64_t unknown_38;
    uint64_t mapped_size;
    uint64_t name_va;
};

_Static_assert(sizeof(struct a53_layout_record_probe) == 0x50,
               "A53 layout record size");

static int probe_layout(void) {
    struct ppr_patch_transport transport;
    a53_transport_make_ppr(&transport, 0);
    uint8_t header[A53_LAYOUT_HEADER_SIZE] = {0};
    if (transport.read(transport.context, A53_LAYOUT_PA, header,
                       sizeof(header)) != (int)sizeof(header)) {
        puts("[!] read-only layout header read failed");
        const uint64_t candidates[] = {
            A53_LAYOUT_PA, 0x70000000ULL, 0x04e28000ULL,
            0x06410000ULL, 0x00000000ULL, 0x00010000ULL,
            0x00020000ULL, 0x00028000ULL, 0x00030000ULL,
            0x00040000ULL, 0x00050000ULL, 0x00060000ULL,
            0x00070000ULL, 0x00080000ULL, 0x00100000ULL,
        };
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]);
             ++i) {
            uint32_t word = 0;
            int read = transport.read(transport.context, candidates[i],
                                      &word, sizeof(word));
            printf("[*] read-only candidate 0x%lx: %s",
                   (unsigned long)candidates[i],
                   read == (int)sizeof(word) ? "ok" : "unavailable");
            if (read == (int)sizeof(word)) printf(" word=0x%08x", word);
            puts("");
        }
        return -1;
    }

    uint32_t count = 0, io_index = 0, dev_index = 0;
    memcpy(&count, header + 0x0c, sizeof(count));
    memcpy(&io_index, header + 0x54, sizeof(io_index));
    memcpy(&dev_index, header + 0x74, sizeof(dev_index));
    if (!count || count > A53_LAYOUT_MAX_RECORDS ||
        io_index >= count || dev_index >= count) {
        printf("[!] invalid layout header: count=%u io=%u dev=%u\n",
               count, io_index, dev_index);
        return -1;
    }

    struct a53_layout_record_probe records[A53_LAYOUT_MAX_RECORDS] = {{0}};
    uint32_t bytes = count * sizeof(records[0]);
    if (transport.read(transport.context, A53_LAYOUT_PA + sizeof(header),
                       records, bytes) != (int)bytes) {
        puts("[!] read-only layout records read failed");
        return -1;
    }
    printf("[*] A53 layout: records=%u io_index=%u dev_index=%u\n",
           count, io_index, dev_index);
    for (uint32_t i = 0; i < count; ++i) {
        const struct a53_layout_record_probe *r = &records[i];
        printf("[*] layout[%u] id=%u flags=0x%x g6=0x%lx+0x%lx "
               "sram=0x%lx+0x%lx mapped=0x%lx+0x%lx%s%s\n",
               i, r->id, r->flags, (unsigned long)r->g6_base,
               (unsigned long)r->g6_size, (unsigned long)r->sram_base,
               (unsigned long)r->sram_size, (unsigned long)r->mapped_base,
               (unsigned long)r->mapped_size,
               i == io_index ? " IO" : "", i == dev_index ? " DEV" : "");
    }
    return 0;
}

static int dump_sram(void) {
    struct ppr_patch_transport transport;
    a53_transport_make_ppr(&transport, 0);
    enum { START = 0x00000, END = 0x90000, CHUNK = 0x1000 };
    uint8_t data[CHUNK];
    fprintf(stdout, "DUMP_BEGIN %x %x\n", START, END);
    for (uint32_t address = START; address < END; address += CHUNK) {
        uint32_t size = END - address;
        if (size > CHUNK) size = CHUNK;
        if (transport.read(transport.context, address, data, size) !=
            (int)size) {
            fprintf(stdout, "DUMP_FAIL %08x\n", address);
            return -1;
        }
        fprintf(stdout, "D %08x ", address);
        for (uint32_t i = 0; i < size; ++i)
            fprintf(stdout, "%02x", data[i]);
        fputc('\n', stdout);
    }
    fputs("DUMP_END\n", stdout);
    return 0;
}

#ifndef PPR_DEFAULT_ACTION
#define PPR_DEFAULT_ACTION PPR_PATCH_STATUS
#endif
#ifndef PPR_DEFAULT_IDLE_ACK
#define PPR_DEFAULT_IDLE_ACK 0
#endif
#ifndef PPR_DEFAULT_FAST
#define PPR_DEFAULT_FAST 0
#endif
#ifndef PPR_DEFAULT_PERSISTENT
#define PPR_DEFAULT_PERSISTENT 0
#endif
#ifndef PPR_DEFAULT_BATCH
#define PPR_DEFAULT_BATCH 0
#endif
#ifndef PPR_DEFAULT_MIXED_IO
#define PPR_DEFAULT_MIXED_IO 0
#endif
#ifndef PPR_DEFAULT_TIME_ACCELERATION
#define PPR_DEFAULT_TIME_ACCELERATION 0
#endif
#ifndef PPR_DEFAULT_PROBE_ONLY
#define PPR_DEFAULT_PROBE_ONLY 0
#endif
#ifndef PPR_DEFAULT_DUMP_SRAM
#define PPR_DEFAULT_DUMP_SRAM 0
#endif

static void raise_fd_limit(void) {
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 &&
        limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        (void)setrlimit(RLIMIT_NOFILE, &limit);
    }
}

static void usage(const char *program) {
    printf("usage: %s [action] [transport options]\n", program);
    puts("actions:");
    puts("  --status                  inspect the installed patch");
    puts("  --probe                   read A53 version/target without a profile or writes");
    puts("  --dump-sram               read A53 SRAM 0x0..0x90000 as hex");
    puts("  --install --idle          install the dynamic selector");
    puts("  --uninstall --idle        restore exact stock bytes");
    puts("  --mode native --idle      select native mode");
    puts("  --mode plaintext-noauth --idle");
    puts("                            select FE/FF plaintext mode");
    puts("transport options (validated before use):");
    puts("  --fast --persistent --batch --mixed-io");
    puts("  --time-acceleration       shorten each phase5 timeout dynamically");
    puts("  --conservative            disable all fast transports");
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    (void)atexit(ppr_notify_flush);
    raise_fd_limit();

    enum ppr_patch_action action =
        (enum ppr_patch_action)PPR_DEFAULT_ACTION;
    int idle_acknowledged = PPR_DEFAULT_IDLE_ACK;
    int fast_mode = PPR_DEFAULT_FAST;
    int time_acceleration = PPR_DEFAULT_TIME_ACCELERATION;
    int probe_only = PPR_DEFAULT_PROBE_ONLY;
    int dump_only = PPR_DEFAULT_DUMP_SRAM;
    struct a53_transport_options options = {
        .persistent = PPR_DEFAULT_PERSISTENT,
        .batch = PPR_DEFAULT_BATCH,
        .mixed_io = PPR_DEFAULT_MIXED_IO,
    };
    int selected = 0;
    int transport_selected = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--status") == 0 ||
            strcmp(argv[i], "--ppr-status") == 0) {
            if (selected++) goto conflict;
            action = PPR_PATCH_STATUS;
        } else if (strcmp(argv[i], "--probe") == 0) {
            if (selected++) goto conflict;
            probe_only = 1;
        } else if (strcmp(argv[i], "--dump-sram") == 0) {
            if (selected++) goto conflict;
            dump_only = 1;
        } else if (strcmp(argv[i], "--install") == 0 ||
                   strcmp(argv[i], "--ppr-install") == 0) {
            if (selected++) goto conflict;
            action = PPR_PATCH_INSTALL;
        } else if (strcmp(argv[i], "--uninstall") == 0 ||
                   strcmp(argv[i], "--ppr-uninstall") == 0) {
            if (selected++) goto conflict;
            action = PPR_PATCH_UNINSTALL;
        } else if ((strcmp(argv[i], "--mode") == 0 ||
                    strcmp(argv[i], "--ppr-mode") == 0) && i + 1 < argc) {
            if (selected++) goto conflict;
            const char *mode = argv[++i];
            if (strcmp(mode, "native") == 0)
                action = PPR_PATCH_MODE_NATIVE;
            else if (strcmp(mode, "plaintext-noauth") == 0)
                action = PPR_PATCH_MODE_PLAINTEXT_NOAUTH;
            else {
                printf("[!] unknown mode: %s\n", mode);
                return 2;
            }
        } else if (strcmp(argv[i], "--idle") == 0 ||
                   strcmp(argv[i], "--i-know-ppr-idle") == 0) {
            idle_acknowledged = 1;
        } else if (strcmp(argv[i], "--fast") == 0) {
            transport_selected = 1;
            fast_mode = 1;
        } else if (strcmp(argv[i], "--persistent") == 0) {
            transport_selected = 1;
            options.persistent = 1;
        } else if (strcmp(argv[i], "--batch") == 0) {
            transport_selected = 1;
            options.batch = 1;
        } else if (strcmp(argv[i], "--mixed-io") == 0) {
            transport_selected = 1;
            options.mixed_io = 1;
        } else if (strcmp(argv[i], "--time-acceleration") == 0 ||
                   strcmp(argv[i], "--short-timeout") == 0) {
            time_acceleration = 1;
        } else if (strcmp(argv[i], "--conservative") == 0) {
            transport_selected = 1;
            fast_mode = 0;
            options.persistent = 0;
            options.batch = 0;
            options.mixed_io = 0;
            time_acceleration = 0;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            printf("[!] unknown or incomplete option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    /* Installation/restoration is fast by default even through the
     * argument-driven payload. Status stays minimal, and an explicit
     * transport selection (especially --conservative) is never overridden. */
    if (action != PPR_PATCH_STATUS && !transport_selected && !fast_mode &&
        !options.persistent && !options.batch && !options.mixed_io) {
        fast_mode = 1;
        options.persistent = 1;
        options.batch = 1;
        options.mixed_io = 1;
    }

    uint32_t system_firmware = kernel_get_fw_version() & 0xffff0000U;
    printf("[+] FW: 0x%x\n", system_firmware);
    if (options.mixed_io && !options.batch) {
        puts("[*] mixed I/O requires batch mode; disabling mixed I/O");
        options.mixed_io = 0;
    }

    const struct a53_transport_options conservative = {0, 0, 0};
    if (a53_transport_initialize(&conservative) != 0)
        return 1;
    if (time_acceleration) {
        if (a53_transport_enable_time_acceleration() != 0) {
            puts("[!] could not safely enable phase5 time acceleration");
            return 1;
        }
    }

    char version[160];
    if (a53_transport_get_version(version, sizeof(version)) != 0) {
        puts("[!] initial GET_CONF failed");
        return 1;
    }
    printf("[*] version string: \"%s\"\n", version);

    uint32_t a53_firmware = a53_transport_parse_release(version);
    if (a53_firmware)
        printf("[*] A53 release parsed as FW 0x%08x\n", a53_firmware);
    else
        puts("[!] A53 release suffix missing; using system firmware");
    if (a53_firmware && a53_firmware != system_firmware)
        printf("[!] system FW 0x%08x != A53 FW 0x%08x\n",
               system_firmware, a53_firmware);

    uint32_t target_firmware = a53_firmware ? a53_firmware : system_firmware;
    enum ppr_target_type target = ppr_patch_parse_target(version);
    printf("[*] A53 target: %s\n", ppr_patch_target_name(target));
    if (dump_only) {
        int dump_result = dump_sram();
        ppr_notify_flush();
        return dump_result == 0 ? 0 : 1;
    }
    if (probe_only) {
        int layout_result = probe_layout();
        puts("[*] read-only probe complete; no profile selected or A53 write attempted");
        ppr_notify_flush();
        return layout_result == 0 ? 0 : 1;
    }
    if (!ppr_patch_target_supported(target_firmware, target)) {
        printf("[!] no exact PPR profile for FW 0x%08x target %s\n",
               target_firmware, ppr_patch_target_name(target));
        return 1;
    }

    int fast_probe = a53_transport_verify_and_enable_fast(&options);
    if (fast_probe < 0) {
        puts("[!] could not initialize the verified fast transport");
        return 1;
    }
    if (fast_probe > 0)
        puts("[*] one or more fast transports failed validation; continuing with verified fallbacks");

    struct ppr_patch_transport transport;
    a53_transport_make_ppr(&transport, fast_mode);
    int result = ppr_patch_run_target(&transport, target_firmware, target,
                                      action, idle_acknowledged);
    ppr_notify_flush();
    return result == 0 ? 0 : 1;

conflict:
    puts("[!] select exactly one action");
    usage(argv[0]);
    return 2;
}
