#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <d3d11.h>
#include <bcrypt.h>
#include <wininet.h>
#include <mmsystem.h>
#include <setupapi.h>

int main() {
    // 1. DirectX 11 API (d3d11.dll)
    D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, NULL, NULL, NULL);

    // 2. Cryptography API (bcrypt.dll)
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCryptOpenAlgorithmProvider(&hAlg, L"SHA256", NULL, 0);

    // 3. Winsock API (ws2_32.dll)
    WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);

    // 4. WinINet API (wininet.dll)
    InternetOpenA("TestAgent", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);

    // 5. Multimedia API (winmm.dll)
    PlaySoundA(NULL, NULL, 0);

    // 6. SetupAPI (setupapi.dll)
    SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_ALLCLASSES);

    return 42;
}
