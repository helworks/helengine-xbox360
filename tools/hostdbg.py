#!/usr/bin/env python3
"""Minimal Win32 debug-loop harness for the emulator (host-side diagnostics).

Launches a process under the Windows debugging API, records loaded modules,
and prints every exception (code, first/second chance, faulting address as
module+offset) plus a raw stack scan of return addresses that fall inside
known modules. It never modifies the target; first-chance exceptions are
passed back unhandled so the target's own handlers (Xenia uses them for guest
memory access) run normally. Standard library only.

Usage: hostdbg.py --timeout 40 --log out.txt -- <exe> [args...]
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import time

k32 = ctypes.windll.kernel32
DEBUG_ONLY_THIS_PROCESS = 0x00000002
CREATE_NEW_CONSOLE = 0x00000010
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION_DEBUG_EVENT = 1
CREATE_THREAD_DEBUG_EVENT = 2
CREATE_PROCESS_DEBUG_EVENT = 3
EXIT_THREAD_DEBUG_EVENT = 4
EXIT_PROCESS_DEBUG_EVENT = 5
LOAD_DLL_DEBUG_EVENT = 6
UNLOAD_DLL_DEBUG_EVENT = 7
OUTPUT_DEBUG_STRING_EVENT = 8
CONTEXT_FULL = 0x10007
CONTEXT_ALL = 0x1003F
IGNORED_CODES = {0x40010006, 0x4001000A, 0x406D1388}  # dbg print, thread name


class STARTUPINFO(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR),
                ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD),
                ("dwYSize", wt.DWORD), ("dwXCountChars", wt.DWORD),
                ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD),
                ("cbReserved2", wt.WORD), ("lpReserved2", ctypes.c_void_p),
                ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE),
                ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


class EXCEPTION_RECORD(ctypes.Structure):
    pass


EXCEPTION_RECORD._fields_ = [
    ("ExceptionCode", wt.DWORD), ("ExceptionFlags", wt.DWORD),
    ("ExceptionRecord", ctypes.POINTER(EXCEPTION_RECORD)),
    ("ExceptionAddress", ctypes.c_void_p), ("NumberParameters", wt.DWORD),
    ("ExceptionInformation", ctypes.c_ulonglong * 15)]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("ExceptionRecord", EXCEPTION_RECORD), ("dwFirstChance", wt.DWORD)]


class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hFile", wt.HANDLE), ("hProcess", wt.HANDLE),
                ("hThread", wt.HANDLE), ("lpBaseOfImage", ctypes.c_void_p),
                ("dwDebugInfoFileOffset", wt.DWORD),
                ("nDebugInfoSize", wt.DWORD), ("lpThreadLocalBase", ctypes.c_void_p),
                ("lpStartAddress", ctypes.c_void_p), ("lpImageName", ctypes.c_void_p),
                ("fUnicode", wt.WORD)]


class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hFile", wt.HANDLE), ("lpBaseOfDll", ctypes.c_void_p),
                ("dwDebugInfoFileOffset", wt.DWORD), ("nDebugInfoSize", wt.DWORD),
                ("lpImageName", ctypes.c_void_p), ("fUnicode", wt.WORD)]


class EXIT_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("dwExitCode", wt.DWORD)]


class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [("Exception", EXCEPTION_DEBUG_INFO),
                ("CreateProcessInfo", CREATE_PROCESS_DEBUG_INFO),
                ("LoadDll", LOAD_DLL_DEBUG_INFO),
                ("ExitProcess", EXIT_PROCESS_DEBUG_INFO),
                ("raw", ctypes.c_byte * 256)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", wt.DWORD), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD), ("u", DEBUG_EVENT_UNION)]


class M128A(ctypes.Structure):
    _fields_ = [("Low", ctypes.c_ulonglong), ("High", ctypes.c_longlong)]


class CONTEXT64(ctypes.Structure):
    _fields_ = [("P1Home", ctypes.c_ulonglong), ("P2Home", ctypes.c_ulonglong),
                ("P3Home", ctypes.c_ulonglong), ("P4Home", ctypes.c_ulonglong),
                ("P5Home", ctypes.c_ulonglong), ("P6Home", ctypes.c_ulonglong),
                ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD),
                ("SegCs", wt.WORD), ("SegDs", wt.WORD), ("SegEs", wt.WORD),
                ("SegFs", wt.WORD), ("SegGs", wt.WORD), ("SegSs", wt.WORD),
                ("EFlags", wt.DWORD), ("Dr0", ctypes.c_ulonglong),
                ("Dr1", ctypes.c_ulonglong), ("Dr2", ctypes.c_ulonglong),
                ("Dr3", ctypes.c_ulonglong), ("Dr6", ctypes.c_ulonglong),
                ("Dr7", ctypes.c_ulonglong), ("Rax", ctypes.c_ulonglong),
                ("Rcx", ctypes.c_ulonglong), ("Rdx", ctypes.c_ulonglong),
                ("Rbx", ctypes.c_ulonglong), ("Rsp", ctypes.c_ulonglong),
                ("Rbp", ctypes.c_ulonglong), ("Rsi", ctypes.c_ulonglong),
                ("Rdi", ctypes.c_ulonglong), ("R8", ctypes.c_ulonglong),
                ("R9", ctypes.c_ulonglong), ("R10", ctypes.c_ulonglong),
                ("R11", ctypes.c_ulonglong), ("R12", ctypes.c_ulonglong),
                ("R13", ctypes.c_ulonglong), ("R14", ctypes.c_ulonglong),
                ("R15", ctypes.c_ulonglong), ("Rip", ctypes.c_ulonglong),
                ("Header", M128A * 2), ("Legacy", M128A * 8),
                ("Xmm", M128A * 16), ("VectorRegister", M128A * 26),
                ("VectorControl", ctypes.c_ulonglong),
                ("DebugControl", ctypes.c_ulonglong),
                ("LastBranchToRip", ctypes.c_ulonglong),
                ("LastBranchFromRip", ctypes.c_ulonglong),
                ("LastExceptionToRip", ctypes.c_ulonglong),
                ("LastExceptionFromRip", ctypes.c_ulonglong)]


def read_mem(process, address, size):
    buffer = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if not k32.ReadProcessMemory(process, ctypes.c_void_p(address), buffer,
                                 size, ctypes.byref(read)):
        return b""
    return buffer.raw[:read.value]


def module_name_from_handle(handle):
    if not handle:
        return None
    buffer = ctypes.create_unicode_buffer(1024)
    length = k32.GetFinalPathNameByHandleW(handle, buffer, 1024, 0)
    return buffer.value[4:] if length and buffer.value.startswith("\\\\?\\") else (buffer.value if length else None)


def image_size(process, base):
    header = read_mem(process, base, 0x400)
    if len(header) < 0x40 or header[:2] != b"MZ":
        return 0
    pe = struct.unpack_from("<i", header, 60)[0]
    if pe + 0x58 > len(header):
        return 0
    return struct.unpack_from("<I", header, pe + 0x50)[0]


class Harness:
    def __init__(self, out):
        self.out = out
        self.modules = []  # (base, size, name)

    def log(self, message):
        line = "[%7.3f] %s" % (time.monotonic() - self.start, message)
        print(line)
        self.out.write(line + "\n")
        self.out.flush()

    def locate(self, address):
        for base, size, name in self.modules:
            if base <= address < base + size:
                return "%s+0x%X" % (name.rsplit("\\", 1)[-1], address - base)
        return None

    def run(self, command_line, timeout):
        self.start = time.monotonic()
        si = STARTUPINFO()
        si.cb = ctypes.sizeof(si)
        pi = PROCESS_INFORMATION()
        k32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p,
                                       ctypes.c_void_p, wt.BOOL, wt.DWORD,
                                       ctypes.c_void_p, wt.LPCWSTR,
                                       ctypes.POINTER(STARTUPINFO),
                                       ctypes.POINTER(PROCESS_INFORMATION)]
        if not k32.CreateProcessW(None, command_line, None, None, False,
                                  DEBUG_ONLY_THIS_PROCESS, None, None,
                                  ctypes.byref(si), ctypes.byref(pi)):
            raise OSError("CreateProcess failed: %d" % k32.GetLastError())
        self.log("started pid %d" % pi.dwProcessId)
        process = pi.hProcess
        event = DEBUG_EVENT()
        exceptions = 0
        while time.monotonic() - self.start < timeout:
            if not k32.WaitForDebugEvent(ctypes.byref(event), 1000):
                continue
            status = DBG_CONTINUE
            code = event.dwDebugEventCode
            if code == CREATE_PROCESS_DEBUG_EVENT:
                info = event.u.CreateProcessInfo
                name = module_name_from_handle(info.hFile) or "main"
                self.modules.append((info.lpBaseOfImage, image_size(process, info.lpBaseOfImage), name))
                self.log("process image %s at 0x%X" % (name, info.lpBaseOfImage))
            elif code == LOAD_DLL_DEBUG_EVENT:
                info = event.u.LoadDll
                name = module_name_from_handle(info.hFile) or "?"
                self.modules.append((info.lpBaseOfDll, image_size(process, info.lpBaseOfDll), name))
                if info.hFile:
                    k32.CloseHandle(info.hFile)
            elif code == EXIT_PROCESS_DEBUG_EVENT:
                self.log("process exited with code 0x%X" % event.u.ExitProcess.dwExitCode)
                k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)
                break
            elif code == EXCEPTION_DEBUG_EVENT:
                info = event.u.Exception
                record = info.ExceptionRecord
                if record.ExceptionCode not in IGNORED_CODES:
                    exceptions += 1
                    address = record.ExceptionAddress or 0
                    where = self.locate(address) or "unknown"
                    params = [record.ExceptionInformation[i]
                              for i in range(min(record.NumberParameters, 4))]
                    self.log("exception 0x%08X %s at 0x%X (%s) tid %d params %s" % (
                        record.ExceptionCode,
                        "first-chance" if info.dwFirstChance else "SECOND-CHANCE",
                        address, where, event.dwThreadId,
                        ["0x%X" % p for p in params]))
                    if record.ExceptionCode != 0x80000003 and exceptions <= 40:
                        self.dump_stack(process, event.dwThreadId)
                    status = DBG_EXCEPTION_NOT_HANDLED
            k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)
        else:
            self.log("timeout reached, terminating target")
            k32.TerminateProcess(process, 1)
            k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE)
        k32.CloseHandle(pi.hThread)
        k32.CloseHandle(pi.hProcess)

    def dump_stack(self, process, thread_id):
        thread = k32.OpenThread(0x001F03FF, False, thread_id)
        if not thread:
            return
        context = CONTEXT64()
        context.ContextFlags = CONTEXT_FULL
        aligned = ctypes.create_string_buffer(ctypes.sizeof(CONTEXT64) + 16)
        offset = (16 - (ctypes.addressof(aligned) % 16)) % 16
        ctx = CONTEXT64.from_buffer(aligned, offset)
        ctx.ContextFlags = CONTEXT_FULL
        if k32.GetThreadContext(thread, ctypes.byref(ctx)):
            self.log("  rip=0x%X (%s) rsp=0x%X rax=0x%X rcx=0x%X rdx=0x%X r8=0x%X r9=0x%X" % (
                ctx.Rip, self.locate(ctx.Rip), ctx.Rsp, ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.R8, ctx.R9))
            stack = read_mem(process, ctx.Rsp, 8 * 512)
            frames = []
            for i in range(0, len(stack) - 7, 8):
                value = struct.unpack_from("<Q", stack, i)[0]
                where = self.locate(value)
                if where and not where.startswith(("ntdll", "KERNEL", "kernel", "user32", "USER32", "win32u")):
                    frames.append("rsp+0x%X: %s" % (i, where))
                if len(frames) >= 24:
                    break
            for frame in frames:
                self.log("    " + frame)
        k32.CloseHandle(thread)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=40)
    parser.add_argument("--log", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    command_line = " ".join('"%s"' % c if " " in c else c for c in command)
    with open(args.log, "w", encoding="utf-8") as out:
        Harness(out).run(command_line, args.timeout)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
