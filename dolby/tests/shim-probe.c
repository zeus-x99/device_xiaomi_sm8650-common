typedef unsigned long long u64;
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern void call_reflector(void *, u64 *);

__asm__(".text\n.global call_reflector\ncall_reflector:\n"
        "mov x9, x0\nmov x8, x1\nmov x0, xzr\nbr x9\n");

void _start(void) {
    void *lib = dlopen("/vendor/lib64/libshim.so", 2);
    void *fn = lib ? dlsym(lib, "_ZN7android13FilterWrapper17getParamReflectorEv") : 0;
    u64 slots[4] = {0x1234, 0xa5a5, 0x5a5a, 0x5678};
    if (fn) call_reflector(fn, slots + 1);
    int result = !fn ? 2 : (slots[0] != 0x1234 || slots[1] || slots[2] || slots[3] != 0x5678);
    register long code __asm__("x0") = result;
    register long syscall __asm__("x8") = 93;
    __asm__ volatile("svc #0" : : "r"(code), "r"(syscall) : "memory");
    __builtin_unreachable();
}
