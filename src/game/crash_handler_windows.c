#include <windows.h>
#ifdef _MSC_VER
#include <dbghelp.h>
// Same line-number codes dwarfstack passes to errPrint()
#define DWST_BASE_ADDR   -1
#define DWST_NOT_FOUND   -2
#define DWST_NO_DBG_SYM  -3
#define DWST_NO_SRC_FILE -4
#else
#include <dwarfstack.h>
#endif

#include <stdio.h>
#include <string.h>

#include "astonia.h"
#include "../sdl/sdl.h"

extern FILE *errorfp;

static void main_dump(FILE *fp)
{
	int i;
	unsigned long long tmp;

	fprintf(fp, "Main datadump:\n");

	fprintf(fp, "game_options: %llu\n", (unsigned long long)game_options);
	for (i = 0; i < 64; i++) {
		tmp = 1llu << i;
		if (game_options & tmp) {
			fprintf(fp, "game_option: %llu\n", tmp);
		}
	}


	fprintf(fp, "\n");
}

static void errPrint(uint64_t addr, const char *filename, int lineno, const char *funcname, void *context, int columnno)
{
	int *count = context;
	const char *delim = strrchr(filename, '/');
	if (delim) {
		filename = delim + 1;
	}
	delim = strrchr(filename, '\\');
	if (delim) {
		filename = delim + 1;
	}

	void *ptr = (void *)(uintptr_t)addr;
	switch (lineno) {
	case DWST_BASE_ADDR:
		fprintf(stderr, "base address: 0x%p (%s)\n", ptr, filename);
		fprintf(errorfp, "base address: 0x%p (%s)\n", ptr, filename);
		break;

	case DWST_NOT_FOUND:
	case DWST_NO_DBG_SYM:
	case DWST_NO_SRC_FILE:
		fprintf(stderr, "    stack %02d: 0x%p (%s)\n", *count, ptr, filename);
		fprintf(errorfp, "    stack %02d: 0x%p (%s)\n", *count, ptr, filename);
		(*count)++;
		break;

	default:
		if (ptr) {
			fprintf(stderr, "    stack %02d: 0x%p", *count, ptr);
			fprintf(errorfp, "    stack %02d: 0x%p", *count, ptr);
			(*count)++;
		} else {
			fprintf(stderr, "                %*s", (int)sizeof(void *) * 2, "");
			fprintf(errorfp, "                %*s", (int)sizeof(void *) * 2, "");
		}
		fprintf(stderr, " (%s:%d", filename, lineno);
		fprintf(errorfp, " (%s:%d", filename, lineno);
		if (columnno > 0) {
			fprintf(stderr, ":%d", columnno);
			fprintf(errorfp, ":%d", columnno);
		}
		fprintf(stderr, ")");
		fprintf(errorfp, ")");
		if (funcname) {
			fprintf(stderr, " [%s]", funcname);
			fprintf(errorfp, " [%s]", funcname);
		}
		fprintf(stderr, "\n");
		fprintf(errorfp, "\n");
		break;
	}
}

#ifdef _MSC_VER
// MSVC / clang-cl build: dwarfstack only reads DWARF (MinGW) debug info, so walk
// the stack with DbgHelp instead. Function names and lines come from moac.pdb.
static void dbghelp_print_stack(CONTEXT *ctx, int *count)
{
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();
	CONTEXT c = *ctx;
	STACKFRAME64 frame;
	DWORD64 last_base = 0;
	char modname[MAX_PATH];

	union {
		SYMBOL_INFO info;
		char buf[sizeof(SYMBOL_INFO) + 256];
	} sym;

	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	SymInitialize(process, NULL, TRUE);

	memset(&frame, 0, sizeof(frame));
	frame.AddrPC.Offset = c.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = c.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = c.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	for (int depth = 0; depth < 64; depth++) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &c, NULL, SymFunctionTableAccess64,
		        SymGetModuleBase64, NULL)) {
			break;
		}
		DWORD64 addr = frame.AddrPC.Offset;
		if (!addr) {
			break;
		}

		DWORD64 base = SymGetModuleBase64(process, addr);
		modname[0] = 0;
		if (!base || !GetModuleFileNameA((HMODULE)(uintptr_t)base, modname, sizeof(modname))) {
			strcpy(modname, "?");
		}
		if (base && base != last_base) {
			errPrint(base, modname, DWST_BASE_ADDR, NULL, count, 0);
			last_base = base;
		}

		memset(&sym, 0, sizeof(sym));
		sym.info.SizeOfStruct = sizeof(SYMBOL_INFO);
		sym.info.MaxNameLen = 255;
		DWORD64 symdisp = 0;
		const char *funcname = SymFromAddr(process, addr, &symdisp, &sym.info) ? sym.info.Name : NULL;

		IMAGEHLP_LINE64 line;
		DWORD linedisp = 0;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);
		if (SymGetLineFromAddr64(process, addr, &linedisp, &line)) {
			errPrint(addr, line.FileName, (int)line.LineNumber, funcname, count, 0);
		} else if (funcname) {
			errPrint(addr, modname, 0, funcname, count, 0);
		} else {
			errPrint(addr, modname, DWST_NO_DBG_SYM, NULL, count, 0);
		}
	}

	SymCleanup(process);
}
#endif

static LONG WINAPI exceptionPrinter(LPEXCEPTION_POINTERS ep)
{
	void xlog(FILE * logfp, char *format, ...);
	void sdl_dump(FILE * fp);
	void render_dump(FILE * fp);
	void gui_dump(FILE * fp);
	char filename[MAX_PATH + 128];

	fprintf(stderr, "\nApplication crashed!\n\n");
	fprintf(errorfp, "\n\n");
	xlog(errorfp, "Application crashed!\n");

	main_dump(stderr);
	main_dump(errorfp);
	sdl_dump(stderr);
	sdl_dump(errorfp);
	render_dump(stderr);
	render_dump(errorfp);
	gui_dump(stderr);
	gui_dump(errorfp);

	DWORD code = ep->ExceptionRecord->ExceptionCode;
	const char *desc = "";
	switch (code) {
#define EX_DESC(name)                                                                                                  \
	case EXCEPTION_##name:                                                                                             \
		desc = " (" #name ")";                                                                                         \
		break

		EX_DESC(ACCESS_VIOLATION);
		EX_DESC(ARRAY_BOUNDS_EXCEEDED);
		EX_DESC(BREAKPOINT);
		EX_DESC(DATATYPE_MISALIGNMENT);
		EX_DESC(FLT_DENORMAL_OPERAND);
		EX_DESC(FLT_DIVIDE_BY_ZERO);
		EX_DESC(FLT_INEXACT_RESULT);
		EX_DESC(FLT_INVALID_OPERATION);
		EX_DESC(FLT_OVERFLOW);
		EX_DESC(FLT_STACK_CHECK);
		EX_DESC(FLT_UNDERFLOW);
		EX_DESC(ILLEGAL_INSTRUCTION);
		EX_DESC(IN_PAGE_ERROR);
		EX_DESC(INT_DIVIDE_BY_ZERO);
		EX_DESC(INT_OVERFLOW);
		EX_DESC(INVALID_DISPOSITION);
		EX_DESC(NONCONTINUABLE_EXCEPTION);
		EX_DESC(PRIV_INSTRUCTION);
		EX_DESC(SINGLE_STEP);
		EX_DESC(STACK_OVERFLOW);
	}
	fprintf(stderr, "code: 0x%08lX%s\n", code, desc);
	fprintf(errorfp, "code: 0x%08lX%s\n", code, desc);

	if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters == 2) {
		ULONG_PTR flag = ep->ExceptionRecord->ExceptionInformation[0];
		ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
		fprintf(stderr, "%s violation at 0x%p\n",
		    flag == 8 ? "data execution prevention" : (flag ? "write access" : "read access"), (void *)addr);
		fprintf(errorfp, "%s violation at 0x%p\n",
		    flag == 8 ? "data execution prevention" : (flag ? "write access" : "read access"), (void *)addr);
	}

	int count = 0;
#ifdef _MSC_VER
	dbghelp_print_stack(ep->ContextRecord, &count);
#else
	dwstOfException(ep->ContextRecord, &errPrint, &count);
#endif

	fflush(stderr);
	fflush(errorfp);
	fclose(errorfp);

	if (localdata) {
		sprintf(filename, "Details written to %s%s", localdata, "moac.log");
	} else {
		sprintf(filename, "Details written to %s", "moac.log");
	}
	display_messagebox("Application Crashed", filename);

#ifdef DEVELOPER
	sdl_dump_spritecache();
#endif

	return (EXCEPTION_EXECUTE_HANDLER);
}

void register_crash_handler(void)
{
	SetUnhandledExceptionFilter(exceptionPrinter);
}
