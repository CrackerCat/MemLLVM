// ============================================================================
// MemLLVM-LTL Freestanding Standard I/O (stdio) Implementation
// Provides complete, self-contained __stdio_common_*, __acrt_iob_func, fflush,
// printf, wprintf, and stdio streams independent of host UCRT.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdarg>

#pragma bss_seg(".data")
#pragma data_seg(".data")

static char __ltl_fake_stdin[16]  = { 0 };
static char __ltl_fake_stdout[16] = { 0 };
static char __ltl_fake_stderr[16] = { 0 };

static HANDLE get_output_handle(void* stream) {
    if (stream == __ltl_fake_stderr || stream == reinterpret_cast<void*>(2) || stream == reinterpret_cast<void*>(3)) {
        HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hErr && hErr != INVALID_HANDLE_VALUE) return hErr;
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) {
        hOut = GetStdHandle(STD_ERROR_HANDLE);
    }
    return hOut;
}

static void write_wide_to_handle(HANDLE h, const wchar_t* wstr, size_t wlen) {
    if (!h || h == INVALID_HANDLE_VALUE || !wstr || wlen == 0) return;
    DWORD written = 0;
    if (GetFileType(h) == FILE_TYPE_CHAR && WriteConsoleW(h, wstr, static_cast<DWORD>(wlen), &written, nullptr)) {
        return;
    }

    // Convert wide characters to UTF-8 for pipes, files, and redirected streams
    char u8[512];
    size_t u8_pos = 0;
    for (size_t i = 0; i < wlen; ++i) {
        unsigned int code = static_cast<unsigned int>(wstr[i]);
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < wlen) {
            unsigned int low = static_cast<unsigned int>(wstr[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + (((code & 0x3FF) << 10) | (low & 0x3FF));
                ++i;
            }
        }
        if (u8_pos + 4 >= sizeof(u8)) {
            WriteFile(h, u8, static_cast<DWORD>(u8_pos), &written, nullptr);
            u8_pos = 0;
        }
        if (code <= 0x7F) {
            u8[u8_pos++] = static_cast<char>(code);
        } else if (code <= 0x7FF) {
            u8[u8_pos++] = static_cast<char>(0xC0 | ((code >> 6) & 0x1F));
            u8[u8_pos++] = static_cast<char>(0x80 | (code & 0x3F));
        } else if (code <= 0xFFFF) {
            u8[u8_pos++] = static_cast<char>(0xE0 | ((code >> 12) & 0x0F));
            u8[u8_pos++] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            u8[u8_pos++] = static_cast<char>(0x80 | (code & 0x3F));
        } else {
            u8[u8_pos++] = static_cast<char>(0xF0 | ((code >> 18) & 0x07));
            u8[u8_pos++] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            u8[u8_pos++] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            u8[u8_pos++] = static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    if (u8_pos > 0) {
        WriteFile(h, u8, static_cast<DWORD>(u8_pos), &written, nullptr);
    }
}

struct WideSink {
    HANDLE h;
    wchar_t* user_buf;
    size_t user_max;
    size_t total_written;
    wchar_t local_buf[512];
    size_t local_count;

    WideSink(HANDLE handle, wchar_t* ubuf = nullptr, size_t umax = 0)
        : h(handle), user_buf(ubuf), user_max(umax), total_written(0), local_count(0) {}

    void flush() {
        if (user_buf) return;
        if (local_count > 0) {
            write_wide_to_handle(h, local_buf, local_count);
            local_count = 0;
        }
    }

    void put(wchar_t c) {
        if (user_buf) {
            if (user_max > 0 && total_written + 1 < user_max) {
                user_buf[total_written] = c;
            }
            total_written++;
        } else {
            if (local_count >= 512) flush();
            local_buf[local_count++] = c;
            total_written++;
        }
    }

    void puts(const wchar_t* s, size_t len) {
        for (size_t i = 0; i < len; ++i) put(s[i]);
    }

    void finish() {
        if (user_buf) {
            if (user_max > 0) {
                size_t null_pos = (total_written < user_max) ? total_written : (user_max - 1);
                user_buf[null_pos] = L'\0';
            }
        } else {
            flush();
        }
    }
};

struct NarrowSink {
    HANDLE h;
    char* user_buf;
    size_t user_max;
    size_t total_written;
    char local_buf[512];
    size_t local_count;

    NarrowSink(HANDLE handle, char* ubuf = nullptr, size_t umax = 0)
        : h(handle), user_buf(ubuf), user_max(umax), total_written(0), local_count(0) {}

    void flush() {
        if (user_buf) return;
        if (local_count > 0) {
            DWORD written = 0;
            WriteFile(h, local_buf, static_cast<DWORD>(local_count), &written, nullptr);
            local_count = 0;
        }
    }

    void put(char c) {
        if (user_buf) {
            if (user_max > 0 && total_written + 1 < user_max) {
                user_buf[total_written] = c;
            }
            total_written++;
        } else {
            if (local_count >= 512) flush();
            local_buf[local_count++] = c;
            total_written++;
        }
    }

    void puts(const char* s, size_t len) {
        for (size_t i = 0; i < len; ++i) put(s[i]);
    }

    void finish() {
        if (user_buf) {
            if (user_max > 0) {
                size_t null_pos = (total_written < user_max) ? total_written : (user_max - 1);
                user_buf[null_pos] = '\0';
            }
        } else {
            flush();
        }
    }
};

static int format_wide(WideSink& sink, const wchar_t* fmt, va_list ap) {
    if (!fmt) return 0;
    while (*fmt) {
        if (*fmt != L'%') {
            sink.put(*fmt++);
            continue;
        }
        fmt++; // skip '%'
        if (*fmt == L'%') {
            sink.put(L'%');
            fmt++;
            continue;
        }

        // 1. Flags
        bool left_align = false;
        bool plus_sign = false;
        bool space_sign = false;
        bool zero_pad = false;
        bool alt_form = false;

        bool parsing_flags = true;
        while (parsing_flags) {
            switch (*fmt) {
                case L'-': left_align = true; fmt++; break;
                case L'+': plus_sign = true; fmt++; break;
                case L' ': space_sign = true; fmt++; break;
                case L'0': zero_pad = true; fmt++; break;
                case L'#': alt_form = true; fmt++; break;
                default: parsing_flags = false; break;
            }
        }

        // 2. Width
        int width = 0;
        if (*fmt == L'*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_align = true;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= L'0' && *fmt <= L'9') {
                width = width * 10 + (*fmt - L'0');
                fmt++;
            }
        }

        // 3. Precision
        int precision = -1;
        if (*fmt == L'.') {
            fmt++;
            if (*fmt == L'*') {
                precision = va_arg(ap, int);
                fmt++;
            } else {
                precision = 0;
                while (*fmt >= L'0' && *fmt <= L'9') {
                    precision = precision * 10 + (*fmt - L'0');
                    fmt++;
                }
            }
        }

        // 4. Length modifier
        int length = 0; // 0=default (32-bit), 1=short (h), 2=char (hh), 3=long (l), 4=long long / 64-bit (ll/I64/z/t)
        if (*fmt == L'h') {
            fmt++;
            if (*fmt == L'h') { length = 2; fmt++; }
            else { length = 1; }
        } else if (*fmt == L'l') {
            fmt++;
            if (*fmt == L'l') { length = 4; fmt++; }
            else { length = 3; }
        } else if (*fmt == L'I') {
            fmt++;
            if (*fmt == L'6' && *(fmt + 1) == L'4') {
                length = 4;
                fmt += 2;
            } else if (*fmt == L'3' && *(fmt + 1) == L'2') {
                length = 0;
                fmt += 2;
            } else {
                length = 4; // %I on x64 is pointer-sized
            }
        } else if (*fmt == L'z' || *fmt == L't' || *fmt == L'j') {
            length = 4;
            fmt++;
        }

        wchar_t spec = *fmt++;
        if (!spec) break;

        // 5. Specifiers
        if (spec == L's' || spec == L'S') {
            bool is_narrow = (spec == L'S') || (length == 1);
            if (is_narrow) {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                size_t len = 0;
                while (s[len]) len++;
                if (precision >= 0 && static_cast<size_t>(precision) < len) len = static_cast<size_t>(precision);
                int pad = (width > static_cast<int>(len)) ? (width - static_cast<int>(len)) : 0;
                if (!left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(L' ');
                }
                for (size_t i = 0; i < len; ++i) {
                    sink.put(static_cast<wchar_t>(static_cast<unsigned char>(s[i])));
                }
                if (left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(L' ');
                }
            } else {
                const wchar_t* s = va_arg(ap, const wchar_t*);
                if (!s) s = L"(null)";
                size_t len = 0;
                while (s[len]) len++;
                if (precision >= 0 && static_cast<size_t>(precision) < len) len = static_cast<size_t>(precision);
                int pad = (width > static_cast<int>(len)) ? (width - static_cast<int>(len)) : 0;
                if (!left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(L' ');
                }
                sink.puts(s, len);
                if (left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(L' ');
                }
            }
        } else if (spec == L'c' || spec == L'C') {
            bool is_narrow = (spec == L'C') || (length == 1);
            wchar_t ch = L'\0';
            if (is_narrow) {
                ch = static_cast<wchar_t>(static_cast<unsigned char>(va_arg(ap, int)));
            } else {
                ch = static_cast<wchar_t>(va_arg(ap, int));
            }
            int pad = (width > 1) ? (width - 1) : 0;
            if (!left_align) {
                for (int i = 0; i < pad; ++i) sink.put(L' ');
            }
            sink.put(ch);
            if (left_align) {
                for (int i = 0; i < pad; ++i) sink.put(L' ');
            }
        } else if (spec == L'd' || spec == L'i') {
            int64_t val = 0;
            if (length == 4) val = va_arg(ap, int64_t);
            else if (length == 3) val = va_arg(ap, long);
            else if (length == 1) val = static_cast<short>(va_arg(ap, int));
            else if (length == 2) val = static_cast<signed char>(va_arg(ap, int));
            else val = va_arg(ap, int);

            bool negative = false;
            uint64_t uval = 0;
            if (val < 0) {
                negative = true;
                uval = static_cast<uint64_t>(-val);
            } else {
                uval = static_cast<uint64_t>(val);
            }

            wchar_t num_buf[32];
            int num_len = 0;
            if (uval == 0) {
                if (precision != 0) num_buf[num_len++] = L'0';
            } else {
                wchar_t rev[32];
                int rev_len = 0;
                while (uval > 0) {
                    rev[rev_len++] = L'0' + static_cast<wchar_t>(uval % 10);
                    uval /= 10;
                }
                for (int i = 0; i < rev_len; ++i) {
                    num_buf[i] = rev[rev_len - 1 - i];
                }
                num_len = rev_len;
            }

            wchar_t sign_ch = 0;
            if (negative) sign_ch = L'-';
            else if (plus_sign) sign_ch = L'+';
            else if (space_sign) sign_ch = L' ';

            int sign_len = (sign_ch != 0) ? 1 : 0;
            int zero_count = (precision > num_len) ? (precision - num_len) : 0;
            if (precision < 0 && zero_pad && !left_align) {
                int avail = width - sign_len - num_len;
                if (avail > zero_count) zero_count = avail;
            }

            int total_len = sign_len + zero_count + num_len;
            int space_pad = (width > total_len) ? (width - total_len) : 0;

            if (!left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(L' ');
            }
            if (sign_ch) sink.put(sign_ch);
            for (int i = 0; i < zero_count; ++i) sink.put(L'0');
            sink.puts(num_buf, num_len);
            if (left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(L' ');
            }
        } else if (spec == L'u' || spec == L'x' || spec == L'X' || spec == L'p') {
            bool is_hex = (spec != L'u');
            bool uppercase = (spec == L'X');
            uint64_t uval = 0;
            if (spec == L'p') {
                uval = reinterpret_cast<uintptr_t>(va_arg(ap, void*));
                alt_form = true;
                length = 4;
            } else {
                if (length == 4) uval = va_arg(ap, uint64_t);
                else if (length == 3) uval = va_arg(ap, unsigned long);
                else if (length == 1) uval = static_cast<unsigned short>(va_arg(ap, unsigned int));
                else if (length == 2) uval = static_cast<unsigned char>(va_arg(ap, unsigned int));
                else uval = va_arg(ap, unsigned int);
            }

            wchar_t num_buf[32];
            int num_len = 0;
            int base = is_hex ? 16 : 10;
            const wchar_t* hex_digits = uppercase ? L"0123456789ABCDEF" : L"0123456789abcdef";

            if (uval == 0) {
                if (spec == L'p') {
                    const wchar_t* null_p = L"(nil)";
                    int nlen = 5;
                    int pad = (width > nlen) ? (width - nlen) : 0;
                    if (!left_align) { for (int i = 0; i < pad; ++i) sink.put(L' '); }
                    sink.puts(null_p, nlen);
                    if (left_align) { for (int i = 0; i < pad; ++i) sink.put(L' '); }
                    continue;
                }
                if (precision != 0) num_buf[num_len++] = L'0';
            } else {
                wchar_t rev[32];
                int rev_len = 0;
                while (uval > 0) {
                    rev[rev_len++] = hex_digits[uval % base];
                    uval /= base;
                }
                for (int i = 0; i < rev_len; ++i) {
                    num_buf[i] = rev[rev_len - 1 - i];
                }
                num_len = rev_len;
            }

            const wchar_t* prefix = nullptr;
            int prefix_len = 0;
            if (is_hex && alt_form && (uval != 0 || spec == L'p')) {
                prefix = uppercase ? L"0X" : L"0x";
                prefix_len = 2;
            }

            int zero_count = (precision > num_len) ? (precision - num_len) : 0;
            if (precision < 0 && zero_pad && !left_align) {
                int avail = width - prefix_len - num_len;
                if (avail > zero_count) zero_count = avail;
            }

            int total_len = prefix_len + zero_count + num_len;
            int space_pad = (width > total_len) ? (width - total_len) : 0;

            if (!left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(L' ');
            }
            if (prefix_len > 0) sink.puts(prefix, prefix_len);
            for (int i = 0; i < zero_count; ++i) sink.put(L'0');
            sink.puts(num_buf, num_len);
            if (left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(L' ');
            }
        } else if (spec == L'f' || spec == L'F') {
            double d = va_arg(ap, double);
            bool negative = false;
            if (d < 0.0) { negative = true; d = -d; }
            int prec = (precision >= 0) ? precision : 6;

            double round_term = 0.5;
            for (int i = 0; i < prec; ++i) round_term /= 10.0;
            d += round_term;

            uint64_t int_part = static_cast<uint64_t>(d);
            double frac_part = d - static_cast<double>(int_part);

            wchar_t num_buf[64];
            int num_len = 0;
            if (int_part == 0) {
                num_buf[num_len++] = L'0';
            } else {
                wchar_t rev[32];
                int rlen = 0;
                while (int_part > 0) {
                    rev[rlen++] = L'0' + static_cast<wchar_t>(int_part % 10);
                    int_part /= 10;
                }
                for (int i = 0; i < rlen; ++i) num_buf[i] = rev[rlen - 1 - i];
                num_len = rlen;
            }

            if (prec > 0 || alt_form) {
                num_buf[num_len++] = L'.';
                for (int i = 0; i < prec; ++i) {
                    frac_part *= 10.0;
                    int digit = static_cast<int>(frac_part);
                    if (digit > 9) digit = 9;
                    num_buf[num_len++] = L'0' + digit;
                    frac_part -= digit;
                }
            }

            wchar_t sign_ch = 0;
            if (negative) sign_ch = L'-';
            else if (plus_sign) sign_ch = L'+';
            else if (space_sign) sign_ch = L' ';

            int sign_len = (sign_ch != 0) ? 1 : 0;
            int space_pad = (width > (sign_len + num_len)) ? (width - (sign_len + num_len)) : 0;

            if (!left_align && (!zero_pad || space_pad == 0)) {
                for (int i = 0; i < space_pad; ++i) sink.put(L' ');
            }
            if (sign_ch) sink.put(sign_ch);
            if (!left_align && zero_pad) {
                for (int i = 0; i < space_pad; ++i) sink.put(L'0');
            }
            sink.puts(num_buf, num_len);
            if (left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(L' ');
            }
        } else {
            sink.put(spec);
        }
    }
    sink.finish();
    return static_cast<int>(sink.total_written);
}

static int format_narrow(NarrowSink& sink, const char* fmt, va_list ap) {
    if (!fmt) return 0;
    while (*fmt) {
        if (*fmt != '%') {
            sink.put(*fmt++);
            continue;
        }
        fmt++; // skip '%'
        if (*fmt == '%') {
            sink.put('%');
            fmt++;
            continue;
        }

        bool left_align = false;
        bool plus_sign = false;
        bool space_sign = false;
        bool zero_pad = false;
        bool alt_form = false;

        bool parsing_flags = true;
        while (parsing_flags) {
            switch (*fmt) {
                case '-': left_align = true; fmt++; break;
                case '+': plus_sign = true; fmt++; break;
                case ' ': space_sign = true; fmt++; break;
                case '0': zero_pad = true; fmt++; break;
                case '#': alt_form = true; fmt++; break;
                default: parsing_flags = false; break;
            }
        }

        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_align = true;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                fmt++;
            } else {
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        int length = 0;
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { length = 2; fmt++; }
            else { length = 1; }
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { length = 4; fmt++; }
            else { length = 3; }
        } else if (*fmt == 'I') {
            fmt++;
            if (*fmt == '6' && *(fmt + 1) == '4') {
                length = 4;
                fmt += 2;
            } else if (*fmt == '3' && *(fmt + 1) == '2') {
                length = 0;
                fmt += 2;
            } else {
                length = 4;
            }
        } else if (*fmt == 'z' || *fmt == 't' || *fmt == 'j') {
            length = 4;
            fmt++;
        }

        char spec = *fmt++;
        if (!spec) break;

        if (spec == 's' || spec == 'S') {
            bool is_wide = (spec == 'S') || (length == 3) || (length == 4);
            if (is_wide) {
                const wchar_t* ws = va_arg(ap, const wchar_t*);
                if (!ws) ws = L"(null)";
                size_t len = 0;
                while (ws[len]) len++;
                if (precision >= 0 && static_cast<size_t>(precision) < len) len = static_cast<size_t>(precision);
                int pad = (width > static_cast<int>(len)) ? (width - static_cast<int>(len)) : 0;
                if (!left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(' ');
                }
                for (size_t i = 0; i < len; ++i) {
                    sink.put(static_cast<char>(ws[i] & 0xFF));
                }
                if (left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(' ');
                }
            } else {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                size_t len = 0;
                while (s[len]) len++;
                if (precision >= 0 && static_cast<size_t>(precision) < len) len = static_cast<size_t>(precision);
                int pad = (width > static_cast<int>(len)) ? (width - static_cast<int>(len)) : 0;
                if (!left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(' ');
                }
                sink.puts(s, len);
                if (left_align) {
                    for (int i = 0; i < pad; ++i) sink.put(' ');
                }
            }
        } else if (spec == 'c' || spec == 'C') {
            char ch = static_cast<char>(va_arg(ap, int));
            int pad = (width > 1) ? (width - 1) : 0;
            if (!left_align) {
                for (int i = 0; i < pad; ++i) sink.put(' ');
            }
            sink.put(ch);
            if (left_align) {
                for (int i = 0; i < pad; ++i) sink.put(' ');
            }
        } else if (spec == 'd' || spec == 'i') {
            int64_t val = 0;
            if (length == 4) val = va_arg(ap, int64_t);
            else if (length == 3) val = va_arg(ap, long);
            else if (length == 1) val = static_cast<short>(va_arg(ap, int));
            else if (length == 2) val = static_cast<signed char>(va_arg(ap, int));
            else val = va_arg(ap, int);

            bool negative = false;
            uint64_t uval = 0;
            if (val < 0) {
                negative = true;
                uval = static_cast<uint64_t>(-val);
            } else {
                uval = static_cast<uint64_t>(val);
            }

            char num_buf[32];
            int num_len = 0;
            if (uval == 0) {
                if (precision != 0) num_buf[num_len++] = '0';
            } else {
                char rev[32];
                int rev_len = 0;
                while (uval > 0) {
                    rev[rev_len++] = '0' + static_cast<char>(uval % 10);
                    uval /= 10;
                }
                for (int i = 0; i < rev_len; ++i) {
                    num_buf[i] = rev[rev_len - 1 - i];
                }
                num_len = rev_len;
            }

            char sign_ch = 0;
            if (negative) sign_ch = '-';
            else if (plus_sign) sign_ch = '+';
            else if (space_sign) sign_ch = ' ';

            int sign_len = (sign_ch != 0) ? 1 : 0;
            int zero_count = (precision > num_len) ? (precision - num_len) : 0;
            if (precision < 0 && zero_pad && !left_align) {
                int avail = width - sign_len - num_len;
                if (avail > zero_count) zero_count = avail;
            }

            int total_len = sign_len + zero_count + num_len;
            int space_pad = (width > total_len) ? (width - total_len) : 0;

            if (!left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(' ');
            }
            if (sign_ch) sink.put(sign_ch);
            for (int i = 0; i < zero_count; ++i) sink.put('0');
            sink.puts(num_buf, num_len);
            if (left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(' ');
            }
        } else if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'p') {
            bool is_hex = (spec != 'u');
            bool uppercase = (spec == 'X');
            uint64_t uval = 0;
            if (spec == 'p') {
                uval = reinterpret_cast<uintptr_t>(va_arg(ap, void*));
                alt_form = true;
                length = 4;
            } else {
                if (length == 4) uval = va_arg(ap, uint64_t);
                else if (length == 3) uval = va_arg(ap, unsigned long);
                else if (length == 1) uval = static_cast<unsigned short>(va_arg(ap, unsigned int));
                else if (length == 2) uval = static_cast<unsigned char>(va_arg(ap, unsigned int));
                else uval = va_arg(ap, unsigned int);
            }

            char num_buf[32];
            int num_len = 0;
            int base = is_hex ? 16 : 10;
            const char* hex_digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

            if (uval == 0) {
                if (spec == 'p') {
                    const char* null_p = "(nil)";
                    int nlen = 5;
                    int pad = (width > nlen) ? (width - nlen) : 0;
                    if (!left_align) { for (int i = 0; i < pad; ++i) sink.put(' '); }
                    sink.puts(null_p, nlen);
                    if (left_align) { for (int i = 0; i < pad; ++i) sink.put(' '); }
                    continue;
                }
                if (precision != 0) num_buf[num_len++] = '0';
            } else {
                char rev[32];
                int rev_len = 0;
                while (uval > 0) {
                    rev[rev_len++] = hex_digits[uval % base];
                    uval /= base;
                }
                for (int i = 0; i < rev_len; ++i) {
                    num_buf[i] = rev[rev_len - 1 - i];
                }
                num_len = rev_len;
            }

            const char* prefix = nullptr;
            int prefix_len = 0;
            if (is_hex && alt_form && (uval != 0 || spec == 'p')) {
                prefix = uppercase ? "0X" : "0x";
                prefix_len = 2;
            }

            int zero_count = (precision > num_len) ? (precision - num_len) : 0;
            if (precision < 0 && zero_pad && !left_align) {
                int avail = width - prefix_len - num_len;
                if (avail > zero_count) zero_count = avail;
            }

            int total_len = prefix_len + zero_count + num_len;
            int space_pad = (width > total_len) ? (width - total_len) : 0;

            if (!left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(' ');
            }
            if (prefix_len > 0) sink.puts(prefix, prefix_len);
            for (int i = 0; i < zero_count; ++i) sink.put('0');
            sink.puts(num_buf, num_len);
            if (left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(' ');
            }
        } else if (spec == 'f' || spec == 'F') {
            double d = va_arg(ap, double);
            bool negative = false;
            if (d < 0.0) { negative = true; d = -d; }
            int prec = (precision >= 0) ? precision : 6;

            double round_term = 0.5;
            for (int i = 0; i < prec; ++i) round_term /= 10.0;
            d += round_term;

            uint64_t int_part = static_cast<uint64_t>(d);
            double frac_part = d - static_cast<double>(int_part);

            char num_buf[64];
            int num_len = 0;
            if (int_part == 0) {
                num_buf[num_len++] = '0';
            } else {
                char rev[32];
                int rlen = 0;
                while (int_part > 0) {
                    rev[rlen++] = '0' + static_cast<char>(int_part % 10);
                    int_part /= 10;
                }
                for (int i = 0; i < rlen; ++i) num_buf[i] = rev[rlen - 1 - i];
                num_len = rlen;
            }

            if (prec > 0 || alt_form) {
                num_buf[num_len++] = '.';
                for (int i = 0; i < prec; ++i) {
                    frac_part *= 10.0;
                    int digit = static_cast<int>(frac_part);
                    if (digit > 9) digit = 9;
                    num_buf[num_len++] = '0' + digit;
                    frac_part -= digit;
                }
            }

            char sign_ch = 0;
            if (negative) sign_ch = '-';
            else if (plus_sign) sign_ch = '+';
            else if (space_sign) sign_ch = ' ';

            int sign_len = (sign_ch != 0) ? 1 : 0;
            int space_pad = (width > (sign_len + num_len)) ? (width - (sign_len + num_len)) : 0;

            if (!left_align && (!zero_pad || space_pad == 0)) {
                for (int i = 0; i < space_pad; ++i) sink.put(' ');
            }
            if (sign_ch) sink.put(sign_ch);
            if (!left_align && zero_pad) {
                for (int i = 0; i < space_pad; ++i) sink.put('0');
            }
            sink.puts(num_buf, num_len);
            if (left_align) {
                for (int i = 0; i < space_pad; ++i) sink.put(' ');
            }
        } else {
            sink.put(spec);
        }
    }
    sink.finish();
    return static_cast<int>(sink.total_written);
}

extern "C" {

void* __cdecl __acrt_iob_func(unsigned int idx) {
    if (idx == 0) return __ltl_fake_stdin;
    if (idx == 1) return __ltl_fake_stdout;
    if (idx == 2) return __ltl_fake_stderr;
    return __ltl_fake_stdout;
}

/* _fileno / _setmode —— 必须由 ltl 自己回答。
 *
 * CRT 初始化代码（以及常见的 kull_m_output_init 之类）会拿 stdout/stderr 去问
 * fd 号，再切成 _O_U8TEXT 文本模式。但 __acrt_iob_func 返回的是本文件里的哑
 * 结构，交给宿主 UCRT 的 _fileno 会被当成 FILE* 解引用，读出一个越界的垃圾 fd；
 * 紧接着 _setmode 对该 fd 做范围校验，走 _invalid_parameter 直接终止进程 ——
 * 表现就是「banner 一个字都没打出来，进程就没了」。
 *
 * 输出编码由 __stdio_common_vfwprintf 内部完成（UTF-16 -> UTF-8 -> WriteFile），
 * 不依赖 CRT 的文本模式，所以 _setmode 只需接受调用并如实回报旧模式。
 */
int __cdecl _fileno(void* stream) {
    if (stream == __ltl_fake_stdin)  return 0;
    if (stream == __ltl_fake_stdout) return 1;
    if (stream == __ltl_fake_stderr) return 2;
    return -1;
}

int __cdecl _setmode(int fd, int mode) {
    (void)fd;
    (void)mode;
    return 0x4000; /* _O_TEXT：MSVC 下标准流的默认模式 */
}

void* __cdecl __p__iob(void) {
    return __ltl_fake_stdout;
}

void* __cdecl __iob_func(void) {
    return __ltl_fake_stdout;
}

int __cdecl fflush(void* stream) {
    HANDLE h = get_output_handle(stream);

    /* 只对磁盘句柄真的去刷。
     *
     * ltl 的 stdio 没有用户态缓冲：每次 printf 都是直接 WriteFile 落到句柄，
     * 所以 fflush 本身没有数据要刷；内核里唯一可能积压的是磁盘文件。
     *
     * 对管道调用 FlushFileBuffers 会一直阻塞到对端把数据全部读走。而加载器
     * 的典型写法是「先 WaitForSingleObject 等 payload 线程结束，再去读管道」，
     * 于是两边互相等待 —— payload 卡在 fflush 里退不出去，加载器卡在 wait 里
     * 不来读，stdout 明明已经写进管道却谁也看不到。表现就是「打完 banner
     * 就没反应」。控制台句柄同理，刷它没有任何意义。
     */
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK) {
        FlushFileBuffers(h);
    }
    return 0;
}

int __cdecl __stdio_common_vfwprintf(
    unsigned long long options,
    void* stream,
    const wchar_t* fmt,
    void* locale,
    va_list ap
) {
    (void)options;
    (void)locale;
    HANDLE h = get_output_handle(stream);
    WideSink sink(h);
    return format_wide(sink, fmt, ap);
}

int __cdecl __stdio_common_vfprintf(
    unsigned long long options,
    void* stream,
    const char* fmt,
    void* locale,
    va_list ap
) {
    (void)options;
    (void)locale;
    HANDLE h = get_output_handle(stream);
    NarrowSink sink(h);
    return format_narrow(sink, fmt, ap);
}

int __cdecl __stdio_common_vswprintf(
    unsigned long long options,
    wchar_t* buffer,
    size_t buffer_count,
    const wchar_t* format,
    void* locale,
    va_list ap
) {
    (void)options;
    (void)locale;
    WideSink sink(nullptr, buffer, buffer_count);
    return format_wide(sink, format, ap);
}

int __cdecl __stdio_common_vsprintf(
    unsigned long long options,
    char* buffer,
    size_t buffer_count,
    const char* format,
    void* locale,
    va_list ap
) {
    (void)options;
    (void)locale;
    NarrowSink sink(nullptr, buffer, buffer_count);
    return format_narrow(sink, format, ap);
}

/* _s 变体：MSVC 的 _vsnwprintf_s / _vsnprintf_s 最终落到这两个。
 *
 * 和上面的非 _s 版本只有语义差别：buffer 必须非空、容量必须大于 0，
 * 而且一旦发生截断就返回 -1（调用方据此判定「缓冲区不够」）。format_wide /
 * format_narrow 返回的是「本应写入的长度」，截断时它会 >= buffer_count，
 * 所以用它和容量比较就能识别截断，不需要额外扫描。
 */
int __cdecl __stdio_common_vswprintf_s(
    unsigned long long options,
    wchar_t* buffer,
    size_t buffer_count,
    const wchar_t* format,
    void* locale,
    va_list ap
) {
    int r;
    (void)options;
    (void)locale;
    if (!buffer || buffer_count == 0)
        return -1;
    WideSink sink(nullptr, buffer, buffer_count);
    r = format_wide(sink, format, ap);
    if (r < 0 || static_cast<size_t>(r) >= buffer_count)
        return -1;
    return r;
}

int __cdecl __stdio_common_vsprintf_s(
    unsigned long long options,
    char* buffer,
    size_t buffer_count,
    const char* format,
    void* locale,
    va_list ap
) {
    int r;
    (void)options;
    (void)locale;
    if (!buffer || buffer_count == 0)
        return -1;
    NarrowSink sink(nullptr, buffer, buffer_count);
    r = format_narrow(sink, format, ap);
    if (r < 0 || static_cast<size_t>(r) >= buffer_count)
        return -1;
    return r;
}

int __cdecl __stdio_common_vsnprintf_s(
    unsigned long long options,
    char* buffer,
    size_t buffer_count,
    size_t max_count,
    const char* format,
    void* locale,
    va_list valist
) {
    size_t count = (buffer_count < max_count) ? buffer_count : max_count;
    return __stdio_common_vsprintf(options, buffer, count, format, locale, valist);
}

int __cdecl __acrt_stdio_flush_and_write_narrow_data(char const c, void* stream, int* written) {
    HANDLE h = get_output_handle(stream);
    DWORD w = 0;
    WriteFile(h, &c, 1, &w, nullptr);
    if (written) *written = static_cast<int>(w);
    return c;
}

int __cdecl __acrt_stdio_flush_and_write_wide_data(wchar_t const c, void* stream, int* written) {
    HANDLE h = get_output_handle(stream);
    write_wide_to_handle(h, &c, 1);
    if (written) *written = 1;
    return c;
}

int __cdecl puts(const char* s) {
    if (!s) return -1;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    size_t len = 0;
    while (s[len]) ++len;
    WriteFile(h, s, static_cast<DWORD>(len), &written, nullptr);
    WriteFile(h, "\n", 1, &written, nullptr);
    return static_cast<int>(len + 1);
}

int __cdecl fputs(const char* s, void* stream) {
    if (!s) return -1;
    HANDLE h = get_output_handle(stream);
    DWORD written = 0;
    size_t len = 0;
    while (s[len]) ++len;
    WriteFile(h, s, static_cast<DWORD>(len), &written, nullptr);
    return static_cast<int>(len);
}

int __cdecl fputws(const wchar_t* s, void* stream) {
    if (!s) return -1;
    HANDLE h = get_output_handle(stream);
    size_t len = 0;
    while (s[len]) ++len;
    write_wide_to_handle(h, s, len);
    return static_cast<int>(len);
}

int __cdecl putchar(int c) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) h = GetStdHandle(STD_ERROR_HANDLE);
    char ch = static_cast<char>(c);
    DWORD written = 0;
    WriteFile(h, &ch, 1, &written, nullptr);
    return c;
}

int __cdecl fputc(int c, void* stream) {
    HANDLE h = get_output_handle(stream);
    char ch = static_cast<char>(c);
    DWORD written = 0;
    WriteFile(h, &ch, 1, &written, nullptr);
    return c;
}

int __cdecl putwchar(wchar_t c) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) h = GetStdHandle(STD_ERROR_HANDLE);
    write_wide_to_handle(h, &c, 1);
    return c;
}

int __cdecl fputwc(wchar_t c, void* stream) {
    HANDLE h = get_output_handle(stream);
    write_wide_to_handle(h, &c, 1);
    return c;
}

size_t __cdecl fwrite(const void* buffer, size_t size, size_t count, void* stream) {
    if (!buffer || size == 0 || count == 0) return 0;
    HANDLE h = get_output_handle(stream);
    DWORD written = 0;
    size_t total = size * count;
    WriteFile(h, buffer, static_cast<DWORD>(total), &written, nullptr);
    return written / size;
}

size_t __cdecl fread(void* buffer, size_t size, size_t count, void* stream) {
    (void)stream;
    if (!buffer || size == 0 || count == 0) return 0;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) return 0;
    DWORD bytesRead = 0;
    size_t total = size * count;
    if (ReadFile(h, buffer, static_cast<DWORD>(total), &bytesRead, nullptr)) {
        return bytesRead / size;
    }
    return 0;
}

int __cdecl __stdio_common_vsscanf(
    unsigned long long options,
    const char* buffer,
    size_t buffer_size,
    const char* format,
    void* locale,
    va_list valist
) {
    (void)options;
    (void)locale;
    if (!buffer || !format) return -1;

    const char* b = buffer;
    const char* b_end = (buffer_size == static_cast<size_t>(-1)) ? nullptr : (buffer + buffer_size);
    const char* f = format;
    int items_assigned = 0;

    auto is_space = [](char c) -> bool {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    };

    auto at_end = [&]() -> bool {
        if (b_end) return b >= b_end || *b == '\0';
        return *b == '\0';
    };

    auto skip_whitespace = [&]() {
        while (!at_end() && is_space(*b)) {
            b++;
        }
    };

    while (*f) {
        if (is_space(*f)) {
            skip_whitespace();
            while (is_space(*f)) f++;
            continue;
        }

        if (*f != '%') {
            if (at_end() || *b != *f) {
                return items_assigned;
            }
            b++;
            f++;
            continue;
        }

        f++; // skip '%'
        if (*f == '%') {
            if (at_end() || *b != '%') return items_assigned;
            b++;
            f++;
            continue;
        }

        bool suppress = false;
        if (*f == '*') {
            suppress = true;
            f++;
        }

        int width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }

        int length = 0;
        if (*f == 'h') {
            length = 1;
            f++;
            if (*f == 'h') f++;
        } else if (*f == 'l') {
            length = 3;
            f++;
            if (*f == 'l') { length = 4; f++; }
        } else if (*f == 'I') {
            f++;
            if (*f == '6' && *(f + 1) == '4') { length = 4; f += 2; }
            else if (*f == '3' && *(f + 1) == '2') { length = 0; f += 2; }
            else { length = 4; }
        }

        char spec = *f++;
        if (!spec) break;

        if (spec != 'c') {
            skip_whitespace();
        }

        if (at_end()) {
            return (items_assigned == 0) ? -1 : items_assigned;
        }

        if (spec == 'c') {
            int count = (width > 0) ? width : 1;
            char* out_c = suppress ? nullptr : va_arg(valist, char*);
            for (int i = 0; i < count; ++i) {
                if (at_end()) break;
                if (out_c) out_c[i] = *b;
                b++;
            }
            if (!suppress) items_assigned++;
        } else if (spec == 's') {
            char* out_s = suppress ? nullptr : va_arg(valist, char*);
            int chars_read = 0;
            while (!at_end() && !is_space(*b) && (width <= 0 || chars_read < width)) {
                if (out_s) out_s[chars_read] = *b;
                b++;
                chars_read++;
            }
            if (out_s) out_s[chars_read] = '\0';
            if (chars_read > 0 && !suppress) items_assigned++;
            else if (chars_read == 0) return items_assigned;
        } else if (spec == 'd' || spec == 'i' || spec == 'u') {
            bool negative = false;
            if (*b == '-') {
                negative = true;
                b++;
            } else if (*b == '+') {
                b++;
            }
            int base = 10;
            if (spec == 'i' && *b == '0') {
                if (*(b + 1) == 'x' || *(b + 1) == 'X') {
                    base = 16;
                    b += 2;
                } else {
                    base = 8;
                    b++;
                }
            }
            uint64_t val = 0;
            int digits = 0;
            while (!at_end() && (width <= 0 || digits < width)) {
                char c = *b;
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (base == 16 && c >= 'a' && c <= 'f') d = 10 + (c - 'a');
                else if (base == 16 && c >= 'A' && c <= 'F') d = 10 + (c - 'A');
                if (d < 0 || d >= base) break;
                val = val * base + d;
                b++;
                digits++;
            }
            if (digits == 0) return items_assigned;
            if (!suppress) {
                if (length == 4) {
                    int64_t sval = negative ? -static_cast<int64_t>(val) : static_cast<int64_t>(val);
                    *va_arg(valist, int64_t*) = sval;
                } else if (length == 1) {
                    short sval = negative ? -static_cast<short>(val) : static_cast<short>(val);
                    *va_arg(valist, short*) = sval;
                } else {
                    int sval = negative ? -static_cast<int>(val) : static_cast<int>(val);
                    *va_arg(valist, int*) = sval;
                }
                items_assigned++;
            }
        } else if (spec == 'x' || spec == 'X') {
            if (*b == '0' && (*(b + 1) == 'x' || *(b + 1) == 'X')) {
                b += 2;
            }
            uint64_t val = 0;
            int digits = 0;
            while (!at_end() && (width <= 0 || digits < width)) {
                char c = *b;
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
                else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
                if (d < 0) break;
                val = (val << 4) | d;
                b++;
                digits++;
            }
            if (digits == 0) return items_assigned;
            if (!suppress) {
                if (length == 4) {
                    *va_arg(valist, uint64_t*) = val;
                } else {
                    *va_arg(valist, unsigned int*) = static_cast<unsigned int>(val);
                }
                items_assigned++;
            }
        } else if (spec == 'f' || spec == 'F') {
            bool negative = false;
            if (*b == '-') { negative = true; b++; }
            else if (*b == '+') { b++; }
            double val = 0.0;
            int digits = 0;
            while (!at_end() && *b >= '0' && *b <= '9') {
                val = val * 10.0 + (*b - '0');
                b++;
                digits++;
            }
            if (!at_end() && *b == '.') {
                b++;
                double factor = 0.1;
                while (!at_end() && *b >= '0' && *b <= '9') {
                    val += (*b - '0') * factor;
                    factor *= 0.1;
                    b++;
                    digits++;
                }
            }
            if (digits == 0) return items_assigned;
            if (negative) val = -val;
            if (!suppress) {
                if (length >= 3) {
                    *va_arg(valist, double*) = val;
                } else {
                    *va_arg(valist, float*) = static_cast<float>(val);
                }
                items_assigned++;
            }
        }
    }
    return items_assigned;
}

int __cdecl __stdio_common_vswscanf(
    unsigned long long options,
    const wchar_t* buffer,
    size_t buffer_size,
    const wchar_t* format,
    void* locale,
    va_list valist
) {
    (void)options;
    (void)locale;
    if (!buffer || !format) return -1;

    const wchar_t* b = buffer;
    const wchar_t* b_end = (buffer_size == static_cast<size_t>(-1)) ? nullptr : (buffer + buffer_size);
    const wchar_t* f = format;
    int items_assigned = 0;

    auto is_space = [](wchar_t c) -> bool {
        return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f';
    };

    auto at_end = [&]() -> bool {
        if (b_end) return b >= b_end || *b == L'\0';
        return *b == L'\0';
    };

    auto skip_whitespace = [&]() {
        while (!at_end() && is_space(*b)) {
            b++;
        }
    };

    while (*f) {
        if (is_space(*f)) {
            skip_whitespace();
            while (is_space(*f)) f++;
            continue;
        }

        if (*f != L'%') {
            if (at_end() || *b != *f) {
                return items_assigned;
            }
            b++;
            f++;
            continue;
        }

        f++;
        if (*f == L'%') {
            if (at_end() || *b != L'%') return items_assigned;
            b++;
            f++;
            continue;
        }

        bool suppress = false;
        if (*f == L'*') {
            suppress = true;
            f++;
        }

        int width = 0;
        while (*f >= L'0' && *f <= L'9') {
            width = width * 10 + (*f - L'0');
            f++;
        }

        int length = 0;
        if (*f == L'h') {
            length = 1;
            f++;
            if (*f == L'h') f++;
        } else if (*f == L'l') {
            length = 3;
            f++;
            if (*f == L'l') { length = 4; f++; }
        } else if (*f == L'I') {
            f++;
            if (*f == L'6' && *(f + 1) == L'4') { length = 4; f += 2; }
            else if (*f == L'3' && *(f + 1) == L'2') { length = 0; f += 2; }
            else { length = 4; }
        }

        wchar_t spec = *f++;
        if (!spec) break;

        if (spec != L'c') {
            skip_whitespace();
        }

        if (at_end()) {
            return (items_assigned == 0) ? -1 : items_assigned;
        }

        if (spec == L'c') {
            int count = (width > 0) ? width : 1;
            wchar_t* out_c = suppress ? nullptr : va_arg(valist, wchar_t*);
            for (int i = 0; i < count; ++i) {
                if (at_end()) break;
                if (out_c) out_c[i] = *b;
                b++;
            }
            if (!suppress) items_assigned++;
        } else if (spec == L's') {
            wchar_t* out_s = suppress ? nullptr : va_arg(valist, wchar_t*);
            int chars_read = 0;
            while (!at_end() && !is_space(*b) && (width <= 0 || chars_read < width)) {
                if (out_s) out_s[chars_read] = *b;
                b++;
                chars_read++;
            }
            if (out_s) out_s[chars_read] = L'\0';
            if (chars_read > 0 && !suppress) items_assigned++;
            else if (chars_read == 0) return items_assigned;
        } else if (spec == L'd' || spec == L'i' || spec == L'u') {
            bool negative = false;
            if (*b == L'-') { negative = true; b++; }
            else if (*b == L'+') { b++; }
            int base = 10;
            if (spec == L'i' && *b == L'0') {
                if (*(b + 1) == L'x' || *(b + 1) == L'X') {
                    base = 16;
                    b += 2;
                } else {
                    base = 8;
                    b++;
                }
            }
            uint64_t val = 0;
            int digits = 0;
            while (!at_end() && (width <= 0 || digits < width)) {
                wchar_t c = *b;
                int d = -1;
                if (c >= L'0' && c <= L'9') d = c - L'0';
                else if (base == 16 && c >= L'a' && c <= L'f') d = 10 + (c - L'a');
                else if (base == 16 && c >= L'A' && c <= L'F') d = 10 + (c - L'A');
                if (d < 0 || d >= base) break;
                val = val * base + d;
                b++;
                digits++;
            }
            if (digits == 0) return items_assigned;
            if (!suppress) {
                if (length == 4) {
                    int64_t sval = negative ? -static_cast<int64_t>(val) : static_cast<int64_t>(val);
                    *va_arg(valist, int64_t*) = sval;
                } else if (length == 1) {
                    short sval = negative ? -static_cast<short>(val) : static_cast<short>(val);
                    *va_arg(valist, short*) = sval;
                } else {
                    int sval = negative ? -static_cast<int>(val) : static_cast<int>(val);
                    *va_arg(valist, int*) = sval;
                }
                items_assigned++;
            }
        } else if (spec == L'x' || spec == L'X') {
            if (*b == L'0' && (*(b + 1) == L'x' || *(b + 1) == L'X')) {
                b += 2;
            }
            uint64_t val = 0;
            int digits = 0;
            while (!at_end() && (width <= 0 || digits < width)) {
                wchar_t c = *b;
                int d = -1;
                if (c >= L'0' && c <= L'9') d = c - L'0';
                else if (c >= L'a' && c <= L'f') d = 10 + (c - L'a');
                else if (c >= L'A' && c <= L'F') d = 10 + (c - L'A');
                if (d < 0) break;
                val = (val << 4) | d;
                b++;
                digits++;
            }
            if (digits == 0) return items_assigned;
            if (!suppress) {
                if (length == 4) {
                    *va_arg(valist, uint64_t*) = val;
                } else {
                    *va_arg(valist, unsigned int*) = static_cast<unsigned int>(val);
                }
                items_assigned++;
            }
        }
    }
    return items_assigned;
}

int __cdecl __stdio_common_vfscanf(
    unsigned long long options,
    void* stream,
    const char* format,
    void* locale,
    va_list valist
) {
    (void)options;
    (void)stream;
    (void)format;
    (void)locale;
    (void)valist;
    return -1;
}

} // extern "C"