.code

; DWORD CallShellcodeAsm(LPVOID pMemory)
; RCX = pMemory (Shellcode entry point)
CallShellcodeAsm PROC
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 28h

    call rcx

    add rsp, 28h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ret
CallShellcodeAsm ENDP

END
