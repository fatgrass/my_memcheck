#include "level1.hh"
#include "level2.hh"
#include "level3.hh"

static int mem_tracker(std::string name, pid_t pid)
{
        setenv("LD_BIND_NOW", "1", 1); //FIXME : Potentialy bad
        int status = 0;
        waitpid(pid, &status, 0);

        Breaker b(name, pid);
        Tracker t(name, pid);

        b.add_breakpoint(MAIN_CHILD, b.rr_brk);

        while (1)
        {
                ptrace(PTRACE_CONT, pid, 0, 0);

                waitpid(pid, &status, 0);

                long addr = get_xip(pid);
                auto bp = (void*)(addr - 1);

                if (WIFEXITED(status))
                      break;

                if (WIFSIGNALED(status))
                      break;

                // Segfault
                if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV)
                        break;
                try
                {
                        if (!b.is_from_us(bp))
                                continue;

                        long syscall = NO_SYSCALL;
                        if (bp != b.rr_brk)
                                syscall = get_xax(pid);
                        t.handle_syscall(syscall, b, bp);

                }
                catch (std::logic_error) { break; }
        }

        t.print_mapped_areas();
        ptrace(PTRACE_CONT, pid, 0, 0);
        return 0;
}



int main(int argc, char** argv)
{

        if (argc < 2)
        {
                fprintf(OUT, "Usage: %s [--preload lib] binary_to_trace [ARGS]\n", argv[0]);
                return 0;
        }

        std::string name = argv[1];

        char* preload = get_cmd_opt(argv, argv + argc, "--preload");
        if (preload)
                name = argv[3];
        else
        {
                if (!binary_exists(name) && name.find("--") != std::string::npos)
                {
                        fprintf(OUT, "%sERROR:%s Invalid command option (%s)\n",
                                RED, NONE, name.c_str());
                        exit(-1);
                }
        }

        if (!binary_exists(name) && name.find("./") != std::string::npos)
        {
                fprintf(OUT, "%sERROR:%s Binary %s not found.\n",
                        RED, NONE, name.c_str());
                exit(-1);
        }


        pid_t pid = 0;

        if ((pid = fork()) != 0)
                return mem_tracker(name, pid);

        return run_child(argc - 1, argv + 1, preload);

}
