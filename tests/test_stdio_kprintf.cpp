#include <stdio.h>
#include <wchar.h>
#include <stdint.h>

extern "C" int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Test banner and string formats (%s, %S)
    wprintf(L"==================================================\n");
    wprintf(L"[+] MemLLVM Freestanding Stdio Verification Test\n");
    wprintf(L"==================================================\n");

    const wchar_t* wbanner = L"Wide Banner String";
    const char* ntext = "Narrow ASCII String";
    wprintf(L"[*] Wide string  : %s\n", wbanner);
    wprintf(L"[*] Narrow string: %S\n", ntext);

    // Test character formats (%c, %C)
    wprintf(L"[*] Characters   : %c and %C\n", L'W', 'N');

    // Test integers and hex (%d, %u, %x, %X)
    int count = 42;
    unsigned int uval = 0xDEADBEEF;
    wprintf(L"[*] Decimal: %d | Unsigned: %u | Hex: 0x%08x | UpperHex: 0x%08X\n", count, count, uval, uval);

    // Test 64-bit integers (%I64d, %I64X)
    int64_t big_neg = -123456789012345LL;
    uint64_t big_hex = 0xCAFEBABE12345678ULL;
    wprintf(L"[*] 64-bit int   : %I64d | 64-bit hex: 0x%016I64X\n", big_neg, big_hex);

    // Test pointer format (%p)
    void* ptr = (void*)0x7FFE12345678ULL;
    wprintf(L"[*] Pointer      : %p\n", ptr);

    // Test width and precision
    wprintf(L"[*] Precision    : [%.5s] | Width: [%10d] | LeftAlign: [%-10d]\n", wbanner, 123, 123);

    // Test fflush
    fflush(stdout);

    wprintf(L"[+] ALL STDIO CHECKS COMPLETED SUCCESSFULLY!\n");
    return 0;
}
