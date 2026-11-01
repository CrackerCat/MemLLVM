// ============================================================================
// MemLLVM-LTL Filesystem Runtime (std::filesystem for Flat Machine Code)
// Bridges std::filesystem directly to Win32 File APIs
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

enum class __std_win_error : unsigned long {
    _Success = 0,
    _File_not_found = 2,
    _Path_not_found = 3,
    _Access_denied = 5,
    _Invalid_parameter = 87,
    _Already_exists = 183
};

enum class __std_code_page : unsigned int {
    _Acp = 0,
    _Utf8 = 65001
};

struct __std_fs_convert_result {
    int _Len;
    __std_win_error _Err;
};

enum class __std_fs_file_attr : unsigned long {
    _Readonly = 0x00000001,
    _Hidden = 0x00000002,
    _System = 0x00000004,
    _Directory = 0x00000010,
    _Archive = 0x00000020,
    _Device = 0x00000040,
    _Normal = 0x00000080,
    _Temporary = 0x00000100,
    _Sparse_file = 0x00000200,
    _Reparse_point = 0x00000400,
    _Invalid = 0xFFFFFFFF
};

enum class __std_fs_reparse_tag : unsigned long {
    _None = 0,
    _Mount_point = 0xA0000003L,
    _Symlink = 0xA000000CL
};

enum class __std_fs_stats_flags : unsigned long {
    _None = 0,
    _Follow_symlinks = 0x01,
    _Attributes = 0x02,
    _Reparse_tag = 0x04,
    _File_size = 0x08,
    _Link_count = 0x10,
    _Last_write_time = 0x20
};

struct __std_fs_stats {
    long long _Last_write_time;
    unsigned long long _File_size;
    __std_fs_file_attr _Attributes;
    __std_fs_reparse_tag _Reparse_point_tag;
    unsigned long _Link_count;
    __std_fs_stats_flags _Available;
};

__std_code_page __stdcall __std_fs_code_page() noexcept {
    return __std_code_page::_Utf8;
}

__std_fs_convert_result __stdcall __std_fs_convert_narrow_to_wide(
    __std_code_page _Code_page, const char* _Input_str, int _Input_len, wchar_t* _Output_str, int _Output_len) noexcept {
    UINT cp = (_Code_page == __std_code_page::_Utf8) ? CP_UTF8 : CP_ACP;
    int res = MultiByteToWideChar(cp, 0, _Input_str, _Input_len, _Output_str, _Output_len);
    if (res <= 0) {
        return {0, static_cast<__std_win_error>(GetLastError())};
    }
    return {res, __std_win_error::_Success};
}

__std_fs_convert_result __stdcall __std_fs_convert_wide_to_narrow(
    __std_code_page _Code_page, const wchar_t* _Input_str, int _Input_len, char* _Output_str, int _Output_len) noexcept {
    UINT cp = (_Code_page == __std_code_page::_Utf8) ? CP_UTF8 : CP_ACP;
    int res = WideCharToMultiByte(cp, 0, _Input_str, _Input_len, _Output_str, _Output_len, nullptr, nullptr);
    if (res <= 0) {
        return {0, static_cast<__std_win_error>(GetLastError())};
    }
    return {res, __std_win_error::_Success};
}

__std_fs_convert_result __stdcall __std_fs_convert_wide_to_narrow_replace_chars(
    __std_code_page _Code_page, const wchar_t* _Input_str, int _Input_len, char* _Output_str, int _Output_len) noexcept {
    return __std_fs_convert_wide_to_narrow(_Code_page, _Input_str, _Input_len, _Output_str, _Output_len);
}

__std_win_error __stdcall __std_fs_get_stats(
    const wchar_t* _Path, __std_fs_stats* _Stats, __std_fs_stats_flags _Flags, __std_fs_file_attr) noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(_Path, GetFileExInfoStandard, &data)) {
        return static_cast<__std_win_error>(GetLastError());
    }

    _Stats->_Attributes = static_cast<__std_fs_file_attr>(data.dwFileAttributes);
    _Stats->_File_size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    ULARGE_INTEGER uli;
    uli.LowPart = data.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = data.ftLastWriteTime.dwHighDateTime;
    _Stats->_Last_write_time = static_cast<long long>(uli.QuadPart);
    _Stats->_Reparse_point_tag = __std_fs_reparse_tag::_None;
    _Stats->_Link_count = 1;
    _Stats->_Available = static_cast<__std_fs_stats_flags>(
        static_cast<unsigned long>(__std_fs_stats_flags::_Attributes) |
        static_cast<unsigned long>(__std_fs_stats_flags::_File_size) |
        static_cast<unsigned long>(__std_fs_stats_flags::_Last_write_time));

    return __std_win_error::_Success;
}

size_t __stdcall __std_system_error_allocate_message(
    unsigned long _Message_id, char** _Ptr_str) noexcept {
    constexpr DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD chars = FormatMessageA(flags, nullptr, _Message_id, 0, reinterpret_cast<char*>(_Ptr_str), 0, nullptr);
    return static_cast<size_t>(chars);
}

void __stdcall __std_system_error_deallocate_message(char* _Str) noexcept {
    if (_Str) {
        LocalFree(_Str);
    }
}

} // extern "C"

namespace std {
int __cdecl _Winerror_map(int err) {
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return 2; // ENOENT
    case ERROR_ACCESS_DENIED:
        return 13; // EACCES
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return 12; // ENOMEM
    case ERROR_INVALID_PARAMETER:
        return 22; // EINVAL
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return 17; // EEXIST
    default:
        return err;
    }
}


} // namespace std
