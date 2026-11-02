/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#pragma once
#include "globals.h"
#include <io.h>
#include <fcntl.h>

FILE * logfile;
#if !defined(MIMIKATZ_W2000_SUPPORT)
wchar_t * outputBuffer;
size_t outputBufferElements, outputBufferElementsPosition;
#endif

void kprintf(PCWCHAR format, ...);
void kprintf_inputline(PCWCHAR format, ...);

BOOL kull_m_output_file(PCWCHAR file);

void kull_m_output_init();
void kull_m_output_clean();

/* 当前进程是否有一个可交互的输入终端。
 *
 * 扁平镜像被 loader（shellcode 加载器、注入器）接管时，标准输入通常是一个
 * 空管道或无效句柄：fgetws 会永久阻塞，调用方的 WaitForSingleObject 跟着
 * 卡死，而且 stdout 重定向到管道时连 banner 都读不到。
 * 交互循环必须先问这一句，不可交互就直接跳过，让程序正常走完退出路径。 */
BOOL kull_m_output_isInteractive();