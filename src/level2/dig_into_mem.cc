#include "dig_into_mem.hh"

void* get_r_brk(void* rr_debug, pid_t pid_child)
{
        struct iovec local;
        struct iovec remote;
        char buffer[128] = { 0 };
        local.iov_base = buffer;
        local.iov_len  = sizeof (struct r_debug);;
        remote.iov_base = rr_debug;
        remote.iov_len  = sizeof (struct r_debug);

        process_vm_readv(pid_child, &local, 1, &remote, 1, 0);

        return (void*)reinterpret_cast<struct r_debug*>(buffer)->r_brk;
}

void* get_final_r_debug(Elf64_Dyn* dt_struct, pid_t pid_child)
{
        Elf64_Dyn child_dyn;
        struct iovec local;
        struct iovec remote;

        // Loop until DT_DEBUG
        local.iov_base = &child_dyn;
        local.iov_len = sizeof (Elf64_Dyn);
        remote.iov_base = dt_struct;
        remote.iov_len  = sizeof (Elf64_Dyn);

        while (true)
        {
                for (Elf64_Dyn* cur = dt_struct; ; ++cur)
                {
                        remote.iov_base = cur;
                        process_vm_readv(pid_child, &local, 1, &remote, 1, 0);
                        if (child_dyn.d_tag == DT_DEBUG)
                                break;
                }
                if (child_dyn.d_un.d_ptr)
                        break;

                ptrace(PTRACE_SINGLESTEP, pid_child, NULL, NULL);
                waitpid(pid_child, 0, 0);
        }

        return reinterpret_cast<void*>(child_dyn.d_un.d_ptr);

}

void* get_pt_dynamic(unsigned long phent, unsigned long phnum,
                     pid_t pid_child, void* at_phdr)
{
        // Loop on the Program header until the PT_DYNAMIC entry
        Elf64_Dyn* dt_struct = NULL;
        struct iovec local;
        struct iovec remote;
        char buffer[128];
        Elf64_Phdr* phdr;
        for (unsigned i = 0; i < phnum; ++i)
        {
                local.iov_base = buffer;
                local.iov_len  = sizeof (Elf64_Phdr);
                remote.iov_base = (char*)at_phdr + i * phent;
                remote.iov_len  = sizeof (Elf64_Phdr);

                process_vm_readv(pid_child, &local, 1, &remote, 1, 0);

                phdr = reinterpret_cast<Elf64_Phdr*>(buffer);
                if (phdr->p_type == PT_DYNAMIC)
                {
                        // First DT_XXXX entry
                        dt_struct =
                                reinterpret_cast<Elf64_Dyn*>(phdr->p_vaddr);
                        break;
                }
        }

        if (!dt_struct)
                throw std::logic_error("PT_DYNAMIC not found");

        // FIXME : Deadcode
        // printf("Found _DYNAMIC:\t\t%p\n", (void*)dt_struct);
        return (void*) dt_struct;
}


void* get_phdr(unsigned long& phent, unsigned long& phnum, pid_t pid_child)
{
        // Open proc/[pid]/auxv
        std::ostringstream ss;
        ss << "/proc/" << pid_child << "/auxv";
        auto file = ss.str();
        int fd = open(file.c_str(), std::ios::binary);
        ElfW(auxv_t) auxv_;

        void* at_phdr;

        // Read from flux until getting all the interesting data
        while (read(fd, &auxv_, sizeof (auxv_)) > -1)
        {
                if (auxv_.a_type == AT_PHDR)
                        at_phdr = (void*)auxv_.a_un.a_val;

                if (auxv_.a_type == AT_PHENT)
                        phent = auxv_.a_un.a_val;

                if (auxv_.a_type == AT_PHNUM)
                        phnum = auxv_.a_un.a_val;

                if (phnum && phent && at_phdr)
                        break;
        }
        close(fd);

        return at_phdr;
}

void* get_link_map(void* rr_debug, pid_t pid, int* status)
{
        char buffer[128];
        struct iovec local;
        struct iovec remote;
        local.iov_base  = buffer;
        local.iov_len   = sizeof (struct r_debug);
        remote.iov_base = rr_debug;
        remote.iov_len  = sizeof (Elf64_Phdr);

        process_vm_readv(pid, &local, 1, &remote, 1, 0);

        struct link_map* link_map = ((struct r_debug*)buffer)->r_map;

        // FIXME : Deadcode
        // fprintf(OUT, "Found r_debug->r_map:\t\t%p\n", (void*)link_map);
        *status = ((struct r_debug*)buffer)->r_state;
        return link_map;
}

void print_string_from_mem(void* str, pid_t pid)
{
        char s[64] = {0};
        struct iovec local;
        struct iovec remote;
        local.iov_base  = &s;
        local.iov_len   = sizeof (struct link_map);
        remote.iov_base = str;
        remote.iov_len  = sizeof (struct link_map);

        ssize_t read = process_vm_readv(pid, &local, 1, &remote, 1, 0);

        if (read)
                fprintf(OUT, "%s\n", s);

}

int disass(const char* name, ElfW(Phdr)* phdr, Breaker b, pid_t pid)
{
        csh handle;
        cs_insn *insn = NULL;
        size_t count = 0;

        unsigned char buffer[DISASS_SIZE] = { 0 };
        struct iovec local;
        struct iovec remote;
        local.iov_base  = &buffer;
        local.iov_len   = DISASS_SIZE;
        printf("Phdr: %p\n", (void*) phdr);
        remote.iov_base = phdr;
        remote.iov_len  = DISASS_SIZE;
        unsigned nread = process_vm_readv(pid, &local, 1, &remote, 1, 0);
        printf("%d --> ", nread);
        print_errno();
        if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK)
                return -1;

        cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
        count = cs_disasm(handle, buffer, DISASS_SIZE - 1, (uintptr_t)phdr, 0, &insn);

        if (count > 0)
        {
                for (size_t j = 0; j < count; j++)
                {
                        printf("%lx\t%s\t\t%s\n", insn[j].address, insn[j].mnemonic, insn[j].op_str);
                        if (insn[j].id == X86_INS_SYSCALL)
                                b.add_breakpoint(name, reinterpret_cast<void*>(insn[j].address));
                }

                cs_free(insn, count);
        }
        else
                printf("ERROR: Failed to disassemble given code!\n");

        cs_close(&handle);

        return 0;
}

static void retrieve_infos(const char* name, void* elf_header, pid_t pid, Breaker* b)
{
        ElfW(Ehdr) header;
        char buffer[1024];
        struct iovec local;
        struct iovec remote;
        local.iov_base  = &header;
        local.iov_len   = sizeof (ElfW(Ehdr));
        remote.iov_base = elf_header;
        remote.iov_len  = sizeof (ElfW(Ehdr));
        process_vm_readv(pid, &local, 1, &remote, 1, 0);

        unsigned long phent = 0;
        unsigned long phnum = 0;

        void* at_phdr = get_phdr(phent, phnum, pid);


        for (unsigned i = 0; i < header.e_phnum; ++i)
        {
                local.iov_base = buffer;
                local.iov_len  = sizeof (Elf64_Phdr);
                remote.iov_base = (char*)at_phdr + i * phent;
                remote.iov_len  = sizeof (Elf64_Phdr);

                process_vm_readv(pid, &local, 1, &remote, 1, 0);

                ElfW(Phdr)* phdr = reinterpret_cast<Elf64_Phdr*>(buffer);
                if (phdr->p_flags & PF_X)
                {
                        if (phdr->p_type == PT_LOAD)
                                printf("Executable PT_LOAD Found\n");
                        else if (phdr->p_type == PT_PHDR)
                                printf("Executable PHDR Found\n");
                        else
                                printf("Weird shit Found\n");

                        UNUSED(b);
                        printf("Offset: %lx\n", phdr->p_offset);
                        printf("vaddr: %lx\n", phdr->p_vaddr);
                        printf("paddr: %lx\n", phdr->p_paddr);
                        printf("filesz: %lx\n", phdr->p_filesz);
                        printf("memsz: %lx\n", phdr->p_memsz);
                        disass(name, phdr + phdr->p_vaddr, *b, pid);
                        printf("Addition: %lx\n", (uintptr_t)elf_header + (uintptr_t)phdr->p_vaddr);
                }
        }
}


void browse_link_map(void* link_m, pid_t pid, Breaker* b)
{
        struct link_map map;
        struct iovec local;
        struct iovec remote;
        local.iov_base  = &map;
        local.iov_len   = sizeof (struct link_map);
        remote.iov_base = link_m;
        remote.iov_len  = sizeof (struct link_map);

        process_vm_readv(pid, &local, 1, &remote, 1, 0);

        fprintf(OUT, "\n%sBrowsing link map%s:\n", YELLOW, NONE);

        // FIXME : Useless ? Check if we missed some
        while (map.l_prev)
        {
                remote.iov_base = map.l_prev;
                process_vm_readv(pid, &local, 1, &remote, 1, 0);
        }

        do
        {
                process_vm_readv(pid, &local, 1, &remote, 1, 0);
                if (map.l_addr)
                {
                        // Unlike what the elf.h file can say about it
                        // l_addr is not a difference or any stewpid thing
                        // like that apparently, but the base address the
                        // shared object is loaded at.
                        fprintf(OUT, "l_addr: %p\n", (void*)map.l_addr);
                        fprintf(OUT, "%sl_name%s: ", YELLOW, NONE);
                        print_string_from_mem(map.l_name, pid);
                        fprintf(OUT, "l_ld: %p\n", (void*)map.l_ld);
                        retrieve_infos(map.l_name, (void*)map.l_addr, pid, b);
                        fprintf(OUT, "\n");
                        break;
                }
                remote.iov_base = map.l_next;
        } while (map.l_next);

        fprintf(OUT, "\n");
}
