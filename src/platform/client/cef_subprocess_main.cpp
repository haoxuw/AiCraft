// CEF subprocess shim.
//
// solarium-ui-vk passes this binary's path as CefSettings.browser_subprocess_path
// so Chromium's renderer/GPU/utility children don't re-enter the main game
// (Python init, Vulkan, GLFW window, LLM sidecars). The shim does exactly
// one thing: hand argv to CefExecuteProcess and return its exit code.
//
// CefExecuteProcess returns immediately for the parent (a non-CEF process)
// and never returns for children — control never reaches `return 0`.
//
// We pass the same CefApp the parent uses so OnBeforeCommandLineProcessing
// runs in subprocesses too (forces ANGLE/SwiftShader for OSR reliability).

#include "client/cef_app.h"
#include "include/cef_app.h"

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>

// Diagnostic: dump our inherited FD table + argv to a per-pid file so we can
// see what Chromium is asking us to find at the time we're forked. Gated on
// SOLARIUM_CEF_FD_DEBUG=1 to avoid I/O during normal operation. Each line:
// "fd <N> -> <readlink target>". Argv is stamped at the top of the file.
namespace {
void dumpFdTable(int argc, char* argv[]) {
	// Unconditional during the FD-7 investigation. Toggle off later by
	// re-introducing a SOLARIUM_CEF_FD_DEBUG env-var gate.
	char path[64];
	std::snprintf(path, sizeof(path), "/tmp/solarium_cef_fd_%d.txt", (int)getpid());
	FILE* fp = std::fopen(path, "w");
	if (!fp) return;
	std::fprintf(fp, "pid=%d ppid=%d argc=%d\n", (int)getpid(), (int)getppid(), argc);
	for (int i = 0; i < argc; ++i)
		std::fprintf(fp, "argv[%d]=%s\n", i, argv[i]);
	DIR* d = opendir("/proc/self/fd");
	if (d) {
		while (struct dirent* e = readdir(d)) {
			if (e->d_name[0] == '.') continue;
			char src[128];
			std::snprintf(src, sizeof(src), "/proc/self/fd/%s", e->d_name);
			char dst[512] = "?";
			ssize_t n = readlink(src, dst, sizeof(dst) - 1);
			if (n >= 0) dst[n] = 0;
			std::fprintf(fp, "fd %s -> %s\n", e->d_name, dst);
		}
		closedir(d);
	}
	std::fclose(fp);
}
} // namespace

int main(int argc, char* argv[]) {
	// dumpFdTable was a debug helper that opened FDs 3 and 4 to log the
	// inherited table. That itself broke subsequent CefExecuteProcess —
	// Chromium's argv references FDs 3, 4 (--field-trial-handle, etc.) and
	// our dump's fopen/opendir consumed those slots. Subprocess must keep
	// FDs untouched until CefExecuteProcess takes over.
	CefMainArgs main_args(argc, argv);
	CefRefPtr<CefApp> app = solarium::vk::makeOsrApp();
	return CefExecuteProcess(main_args, app, nullptr);
}
