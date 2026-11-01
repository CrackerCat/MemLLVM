#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#pragma bss_seg(".text_z_bss")
#pragma data_seg(".text_z_data")
#pragma const_seg(".text_z_rodata")

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include "imgui.cpp"
#include "imgui_demo.cpp"
#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"
#include "backends/imgui_impl_win32.cpp"
#include "backends/imgui_impl_dx11.cpp"
#include "examples/example_win32_directx11/main.cpp"
