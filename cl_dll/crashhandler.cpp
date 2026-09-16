#ifdef __ANDROID__

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unwind.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <android/log.h>

#include "crashhandler.h"

static char s_crashLogPath[256] = {0};
static volatile sig_atomic_t s_inCrash = 0;
static char s_engineVersion[64] = {0};
static char s_patcherVersion[64] = {0};

// ─── async-signal-safe helpers ──────────────────────────────────────────────

static size_t safeStrlen(const char *str, size_t maxLen) {
	size_t len = 0;
	while (len < maxLen && str[len]) len++;
	return len;
}

static void safeStrcat(char *dst, const char *src, size_t dstSize) {
	size_t dstLen = safeStrlen(dst, dstSize - 1);
	size_t srcLen = safeStrlen(src, dstSize - 1);
	if (dstLen + srcLen >= dstSize) srcLen = dstSize - dstLen - 1;
	memcpy(dst + dstLen, src, srcLen);
	dst[dstLen + srcLen] = '\0';
}

static void safeIntToHex(char *dst, unsigned long val, size_t dstSize) {
	const char hex[] = "0123456789abcdef";
	char buf[20];
	int i = 0;
	if (val == 0) { dst[0] = '0'; dst[1] = '\0'; return; }
	while (val > 0 && i < 18) { buf[i++] = hex[val & 0xf]; val >>= 4; }
	size_t len = (size_t)i;
	if (len >= dstSize) len = dstSize - 1;
	for (size_t j = 0; j < len; j++) dst[j] = buf[len - 1 - j];
	dst[len] = '\0';
}

static void safeIntToStr(char *dst, int val, size_t dstSize) {
	char buf[16];
	int i = 0, neg = 0;
	if (val < 0) { neg = 1; val = -val; }
	if (val == 0) buf[i++] = '0';
	while (val > 0 && i < 15) { buf[i++] = '0' + (val % 10); val /= 10; }
	if (neg && i < 15) buf[i++] = '-';
	size_t len = (size_t)i;
	if (len >= dstSize) len = dstSize - 1;
	for (size_t j = 0; j < len; j++) dst[j] = buf[len - 1 - j];
	dst[len] = '\0';
}

static const char *getSignalName(int sig) {
	switch (sig) {
		case SIGILL:  return "SIGILL (Illegal instruction)";
		case SIGSEGV: return "SIGSEGV (Segmentation fault)";
		case SIGBUS:  return "SIGBUS (Bus error)";
		case SIGABRT: return "SIGABRT (Abort)";
		case SIGFPE:  return "SIGFPE (Floating point exception)";
		default:      return "UNKNOWN";
	}
}

static int writeStr(int fd, const char *s) {
	int n = (int)safeStrlen(s, 4096);
	if (fd >= 0) {
		ssize_t w = write(fd, s, (size_t)n);
		(void)w;
	}
	// Mirror to logcat: even when no crash.log target is writable (scoped
	// storage / no storage permission), the backtrace still reaches logcat.
	__android_log_write(ANDROID_LOG_ERROR, "CS16Client", s);
	return n;
}

// ─── unwind backtrace ───────────────────────────────────────────────────────
// Read the crashed thread's frame pointer from ucontext registers, then walk
// the frame chain. This works even inside signal handlers where inline asm
// would only see the handler's own stack frames.

// Async-signal-safe probe: is `addr` safe to read 2*sizeof(void*) bytes from?
// Uses the process_vm_readv syscall, which faults without raising SIGSEGV, so
// a garbage frame pointer can never crash the handler mid-backtrace and leave
// a truncated crash.log.
static int isMapped_ro(unsigned long addr) {
	if (addr < 0x10000) return 0;
	unsigned long dummy = 0;
	struct iovec local = { &dummy, sizeof(dummy) };
	struct iovec remote = { (void *)addr, sizeof(dummy) };
	return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)sizeof(dummy);
}

static int getBacktrace(void **buffer, int maxFrames, void *ucontext) {
	int count = 0;
	if (!ucontext) return 0;
	mcontext_t *mctx = &((ucontext_t *)ucontext)->uc_mcontext;

#if defined(__aarch64__)
	// Frame 0 = actual crash PC
	buffer[count++] = (void *)mctx->regs[32];
	// Frame 1 = LR of top frame
	if (mctx->regs[30]) buffer[count++] = (void *)mctx->regs[30];
	// Then walk the frame chain via x29
	void **fp = (void **)mctx->regs[29];
	int guard = 0;
	while (count < maxFrames && fp && !((unsigned long)fp & 0xf) && guard++ < 64) {
		if (!isMapped_ro((unsigned long)fp)) break;
		void *prev = (void *)*fp;
		void *ra = (void *)fp[1];
		if (!ra) break;
		if (prev && (unsigned long)prev <= (unsigned long)fp) break;
		buffer[count++] = ra;
		if (!prev) break;
		if (((unsigned long)prev - (unsigned long)fp) > 0x10000) break;
		fp = (void **)prev;
	}
#elif defined(__arm__)
	buffer[count++] = (void *)mctx->arm_pc;
	if (mctx->arm_lr) buffer[count++] = (void *)mctx->arm_lr;
	void **fp = (void **)mctx->arm_fp;
	int guard = 0;
	while (count < maxFrames && fp && !((unsigned long)fp & 0x3) && guard++ < 64) {
		if (!isMapped_ro((unsigned long)fp)) break;
		void *prev = (void *)*fp;
		void *ra = (void *)fp[1];
		if (!ra) break;
		if (prev && (unsigned long)prev <= (unsigned long)fp) break;
		buffer[count++] = ra;
		if (!prev) break;
		if (((unsigned long)prev - (unsigned long)fp) > 0x10000) break;
		fp = (void **)prev;
	}
#else
	struct BacktraceState { void **cur; void **end; int depth; };
	auto cb = [](_Unwind_Context *ctx, void *arg) -> _Unwind_Reason_Code {
		auto *s = (BacktraceState *)arg;
		void *ip = (void *)_Unwind_GetIP(ctx);
		if (ip && s->cur < s->end) { *s->cur++ = ip; s->depth++; }
		if (s->depth > 256) return _URC_END_OF_STACK;
		return _URC_NO_REASON;
	};
	BacktraceState state = { buffer, buffer + maxFrames, 0 };
	_Unwind_Backtrace(cb, &state);
	count = state.depth;
#endif
	return count;
}

// ─── .symtab reader from disk (mmap, async-signal-safe) ─────────────────────
// map the whole file read-only and parse the section headers in place. No
// large stack buffers: the previous version carried 128KB+64KB stack arrays,
// which overflowed the 8KB alt-signal-stack in the crash handler.
static int tryReadSymtab(const char *so_path,
                          ElfW(Sym) **out_sym, char **out_str, size_t *out_count) {
	*out_sym = NULL; *out_str = NULL; *out_count = 0;

	int fd = open(so_path, O_RDONLY);
	if (fd < 0) return 0;

	struct stat st;
	if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > (off_t)(8 * 1024 * 1024)) {
		close(fd);
		return 0;
	}

	void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return 0;

	size_t size = (size_t)st.st_size;
	char *base = (char *)map;

	if (size < sizeof(ElfW(Ehdr)) || memcmp(base, ELFMAG, SELFMAG) != 0) {
		munmap(map, size);
		return 0;
	}

	ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)base;
	size_t shnum = ehdr->e_shnum;
	size_t shentsize = ehdr->e_shentsize;
	if (shnum == 0 || shentsize < sizeof(ElfW(Shdr))) {
		munmap(map, size);
		return 0;
	}
	size_t sh_total = shnum * shentsize;
	if (ehdr->e_shoff >= size || sh_total > size - ehdr->e_shoff) {
		munmap(map, size);
		return 0;
	}

	ElfW(Shdr) *shdr = (ElfW(Shdr) *)(base + ehdr->e_shoff);

	if (ehdr->e_shstrndx >= shnum) {
		munmap(map, size);
		return 0;
	}
	ElfW(Shdr) *shstr = &shdr[ehdr->e_shstrndx];
	if (shstr->sh_offset >= size || shstr->sh_size > size - shstr->sh_offset) {
		munmap(map, size);
		return 0;
	}
	const char *shstrtab = base + shstr->sh_offset;

	ElfW(Shdr) *symtab_sh = NULL;
	ElfW(Shdr) *strtab_sh = NULL;

	for (size_t i = 0; i < shnum; i++) {
		if (shdr[i].sh_name >= shstr->sh_size) continue;
		const char *name = shstrtab + shdr[i].sh_name;
		if (shdr[i].sh_type == SHT_SYMTAB && strcmp(name, ".symtab") == 0) symtab_sh = &shdr[i];
		if (shdr[i].sh_type == SHT_STRTAB && strcmp(name, ".strtab") == 0) strtab_sh = &shdr[i];
	}

	if (!symtab_sh || !strtab_sh) {
		munmap(map, size);
		return 0;
	}
	if (symtab_sh->sh_offset >= size || symtab_sh->sh_size > size - symtab_sh->sh_offset) {
		munmap(map, size);
		return 0;
	}
	if (strtab_sh->sh_offset >= size || strtab_sh->sh_size > size - strtab_sh->sh_offset ||
	    symtab_sh->sh_entsize == 0) {
		munmap(map, size);
		return 0;
	}

	*out_sym = (ElfW(Sym) *)(base + symtab_sh->sh_offset);
	*out_str = (char *)(base + strtab_sh->sh_offset);
	*out_count = symtab_sh->sh_size / symtab_sh->sh_entsize;
	/* Don't munmap — we're in a crash handler, _exit(1) cleans up */
	return 1;
}

static void findSymtabSymbol(ElfW(Sym) *sym, const char *strtab, size_t count,
                              unsigned long base, unsigned long addr,
                              char *out, size_t outSize) {
	out[0] = '\0';
	const char *best_name = NULL;
	unsigned long best_addr = 0;
	unsigned long best_size = 0;

	for (size_t i = 0; i < count; i++) {
		const ElfW(Sym) *s = &sym[i];
		int type = ELF64_ST_TYPE(s->st_info);
		if (type != STT_FUNC) continue;
		if (s->st_shndx == SHN_UNDEF || s->st_value == 0) continue;
		unsigned long func_addr = base + s->st_value;
		if (func_addr <= addr && func_addr > best_addr) {
			best_addr = func_addr;
			best_size = s->st_size;
			best_name = strtab + s->st_name;
		}
	}

	if (best_name && best_name[0]) {
		unsigned long func_off = addr - best_addr;
		out[0] = '\0';
		safeStrcat(out, best_name, outSize);
		if (func_off > 0) {
			safeStrcat(out, "+0x", outSize);
			char hex[20];
			safeIntToHex(hex, func_off, sizeof(hex));
			safeStrcat(out, hex, outSize);
		}
		if (best_size > 0) {
			safeStrcat(out, " [size=0x", outSize);
			char hex[20];
			safeIntToHex(hex, best_size, sizeof(hex));
			safeStrcat(out, hex, outSize);
			safeStrcat(out, "]", outSize);
		}
	}
}

// ─── memory map dump (async-signal-safe via open/read) ──────────────────────

static void dumpMaps(int fd, unsigned long addr, unsigned long addr30, unsigned long addr16) {
	write(fd, "\n--- Memory Maps (executable) ---\n", 33);

	int maps_fd = open("/proc/self/maps", O_RDONLY);
	if (maps_fd < 0) return;

	char buf[4096];
	int n;
	while ((n = read(maps_fd, buf, sizeof(buf))) > 0) {
		char *line = buf;
		char *end = buf + n;
		while (line < end) {
			char *eol = line;
			while (eol < end && *eol != '\n' && *eol != '\0') eol++;

			if (eol - line > 6 && line[3] == 'x') {
				write(fd, line, eol - line);
				write(fd, "\n", 1);

				unsigned long start = 0, end_addr = 0;
				char *p = line;
				while (p < eol && *p != '-') { start = start * 16 + (*p >= 'a' ? *p - 'a' + 10 : *p >= 'A' ? *p - 'A' + 10 : *p - '0'); p++; }
				p++;
				while (p < eol && *p != ' ') { end_addr = end_addr * 16 + (*p >= 'a' ? *p - 'a' + 10 : *p >= 'A' ? *p - 'A' + 10 : *p - '0'); p++; }

				if (addr >= start && addr < end_addr) write(fd, "    <<< FAULT ADDR\n", 19);
				if (addr30 >= start && addr30 < end_addr) write(fd, "    <<< LR (x30)\n", 17);
				if (addr16 >= start && addr16 < end_addr) write(fd, "    <<< x16\n", 12);
			}

			if (eol < end) line = eol + 1;
			else break;
		}
	}
	close(maps_fd);
}

// ─── ELF symbol resolution via /proc/self/maps (async-signal-safe) ───────────
// We avoid dl_iterate_phdr because it uses pthread_mutex_lock inside Bionic
// and deadlocks in signal handlers. Instead, parse /proc/self/maps directly.

struct MapsEntry {
	unsigned long start;
	unsigned long end;
	unsigned long offset;
	char path[256];
};

// Parse /proc/self/maps to find which library owns an address.
// Returns 1 on success, 0 on failure.
static int findMapsEntry(unsigned long addr, MapsEntry *out) {
	int maps_fd = open("/proc/self/maps", O_RDONLY);
	if (maps_fd < 0) return 0;

	char buf[4096];
	int n;
	int found = 0;

	while ((n = read(maps_fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		char *line = buf;
		while (line < buf + n && !found) {
			char *eol = line;
			while (eol < buf + n && *eol != '\n') eol++;

			// Parse: start-end offset dev inode pathname
			unsigned long start = 0, end = 0, offset = 0;
			char *p = line;

			// Parse start
			while (p < eol && *p >= '0' && *p <= '9') {
				start = start * 16 + (*p >= 'a' ? *p - 'a' + 10 : *p >= 'A' ? *p - 'A' + 10 : *p - '0');
				p++;
			}
			if (p < eol && *p == '-') p++;
			// Parse end
			while (p < eol && *p >= '0' && *p <= '9') {
				end = end * 16 + (*p >= 'a' ? *p - 'a' + 10 : *p >= 'A' ? *p - 'A' + 10 : *p - '0');
				p++;
			}
			// Skip perms (rwxp)
			while (p < eol && *p != ' ') p++;
			if (p < eol) p++;
			// Parse offset
			while (p < eol && *p >= '0' && *p <= '9') {
				offset = offset * 16 + (*p >= 'a' ? *p - 'a' + 10 : *p >= 'A' ? *p - 'A' + 10 : *p - '0');
				p++;
			}

			if (addr >= start && addr < end) {
				// Skip dev + inode, find pathname
				for (int s = 0; s < 2 && p < eol; s++) {
					while (p < eol && *p != ' ') p++;
					if (p < eol) p++;
				}
				// Skip leading spaces
				while (p < eol && *p == ' ') p++;

				size_t plen = (size_t)(eol - p);
				if (plen > 0 && plen < sizeof(out->path)) {
					out->start = start;
					out->end = end;
					out->offset = offset;
					memcpy(out->path, p, plen);
					out->path[plen] = '\0';
					found = 1;
				}
			}

			if (eol < buf + n) line = eol + 1;
			else break;
		}
		if (found) break;
	}
	close(maps_fd);
	return found;
}

// Resolve address using maps + .symtab from disk (all async-signal-safe)
static void resolveAddressEnhanced(char *buf, size_t bufSize, void *addr, int log_fd) {
	unsigned long target = (unsigned long)addr;
	buf[0] = '\0';

	MapsEntry maps;
	memset(&maps, 0, sizeof(maps));

	if (!findMapsEntry(target, &maps)) {
		safeStrcat(buf, "0x", bufSize);
		char hex[20];
		safeIntToHex(hex, target, sizeof(hex));
		safeStrcat(buf, hex, bufSize);
		return;
	}

	// Library name from path
	const char *libName = maps.path;
	const char *slash = strrchr(maps.path, '/');
	if (slash) libName = slash + 1;

	unsigned long offset = target - maps.start + maps.offset;

	safeStrcat(buf, "[", bufSize);
	safeStrcat(buf, libName, bufSize);
	safeStrcat(buf, "+0x", bufSize);
	char hex[20];
	safeIntToHex(hex, offset, sizeof(hex));
	safeStrcat(buf, hex, bufSize);
	safeStrcat(buf, "] ", bufSize);

	// Try .symtab from disk (mmap, async-signal-safe)
	if (maps.path[0]) {
		ElfW(Sym) *sym = NULL;
		char *str = NULL;
		size_t count = 0;
		if (tryReadSymtab(maps.path, &sym, &str, &count)) {
			char symtab_result[256] = {0};
			findSymtabSymbol(sym, str, count, maps.start, target, symtab_result, sizeof(symtab_result));
			if (symtab_result[0]) {
				safeStrcat(buf, symtab_result, bufSize);
			}
			// Don't munmap — we're in a crash handler, _exit(1) cleans up
		}
	}

	// Write addr2line hint
	if (maps.path[0]) {
		char hint[512];
		hint[0] = '\0';
		safeStrcat(hint, "  addr2line -e ", sizeof(hint));
		safeStrcat(hint, maps.path, sizeof(hint));
		safeStrcat(hint, " -f 0x", sizeof(hint));
		safeStrcat(hint, hex, sizeof(hint));
		safeStrcat(hint, "\n", sizeof(hint));
		write(log_fd, hint, strlen(hint));
	}
}

// ─── path helpers ───────────────────────────────────────────────────────────

// Create parent directory of a file path (single level, async-signal-safe).
// Returns 0 on success, -1 if the path is unusable.
static int mkdirParent(const char *path) {
	if (!path || !path[0]) return -1;

	char dir[256];
	dir[0] = '\0';
	safeStrcat(dir, path, sizeof(dir));

	int lastSlash = -1;
	for (int i = 0; dir[i]; i++) {
		if (dir[i] == '/') lastSlash = i;
	}
	if (lastSlash > 0) {
		dir[lastSlash] = '\0';
		if (mkdir(dir, 0755) < 0 && errno != EEXIST) return -1;
	}
	return 0;
}

// Strip leading whitespace/control characters that may sneak into gamedir
// strings (the device showed a folder literally named "\r\ncstrike").
static const char *skipJunk(const char *s) {
	if (!s) return "";
	while (*s && (*s == '\r' || *s == '\n' || *s == ' ' || *s == '\t')) s++;
	return s;
}

static char s_privateCrashPath[256] = {0};

// Candidate crash.log locations, tried in order. The engine's gamedir path
// (s_crashLogPath) is often NOT writable under Android scoped storage (the
// game app has no storage permission and direct open() outside the app's own
// dirs fails on API 30+), which is why users on both arm32 and arm64 saw no
// crash.log at all. The app-private /data/data/<pkg>/files dir is always
// writable and is the reliable last resort.
static const char *crashPathByIndex(int idx) {
	switch (idx) {
		case 0:
			return s_crashLogPath;
		case 1:
			// public download folder (readable by users for bug reports)
			return "/sdcard/Download/CS16Client/crash.log";
		case 2: {
			if (!s_privateCrashPath[0]) {
				char cmd[128];
				cmd[0] = '\0';
				int f = open("/proc/self/cmdline", O_RDONLY);
				if (f >= 0) {
					ssize_t rr = read(f, cmd, sizeof(cmd) - 1);
					close(f);
					if (rr > 0) cmd[rr] = '\0';
				}
				s_privateCrashPath[0] = '\0';
				if (cmd[0]) {
					safeStrcat(s_privateCrashPath, "/data/data/", sizeof(s_privateCrashPath));
					safeStrcat(s_privateCrashPath, cmd, sizeof(s_privateCrashPath));
					safeStrcat(s_privateCrashPath, "/files/crash.log", sizeof(s_privateCrashPath));
				}
			}
			return s_privateCrashPath;
		}
		default:
			return "";
	}
}

// Open the first writable crash.log target. On success s_crashLogPath is set
// to the path that actually opened, so the INIT header and the crash block
// always land in the same file no matter which candidate wins.
static int openCrashLogInternal(int flags) {
	for (int i = 0; i < 3; i++) {
		const char *p = crashPathByIndex(i);
		if (!p[0]) continue;
		if (mkdirParent(p) < 0) continue;
		int fd = open(p, O_WRONLY | O_CREAT | flags, 0644);
		if (fd >= 0) {
			if (strcmp(p, s_crashLogPath) != 0) {
				s_crashLogPath[0] = '\0';
				safeStrcat(s_crashLogPath, p, sizeof(s_crashLogPath));
			}
			return fd;
		}
	}
	return -1;
}

static int openCrashLogAppend(void) {
	return openCrashLogInternal(O_APPEND);
}

static int openCrashLogTrunc(void) {
	return openCrashLogInternal(O_TRUNC);
}

// Resolve the game directory to a full absolute base path. Probes, in order,
// the process CWD (engine usually chdirs to its data dir), then the known
// Android home paths; the first one that actually exists wins.
static int resolveGameDir(const char *gamedir, char *out, size_t outSize) {
	out[0] = '\0';
	const char *gd = skipJunk(gamedir);
	if (!gd[0]) return -1;

	char cwd[256] = {0};

	// Candidate 1: CWD + gamedir
	if (getcwd(cwd, sizeof(cwd))) {
		char cand[256] = {0};
		safeStrcat(cand, cwd, sizeof(cand));
		safeStrcat(cand, "/", sizeof(cand));
		safeStrcat(cand, gd, sizeof(cand));

		struct stat st;
		if (cand[0] && stat(cand, &st) == 0 && S_ISDIR(st.st_mode)) {
			safeStrcat(out, cand, outSize);
			return 0;
		}
	}

	// Candidate 2/3: known Android xash homes
	static const char *homes[] = {
		"/storage/emulated/0/xash",
		"/sdcard/xash",
	};
	for (size_t i = 0; i < sizeof(homes) / sizeof(homes[0]); i++) {
		char cand[256] = {0};
		safeStrcat(cand, homes[i], sizeof(cand));
		safeStrcat(cand, "/", sizeof(cand));
		safeStrcat(cand, gd, sizeof(cand));

		struct stat st;
		if (cand[0] && stat(cand, &st) == 0 && S_ISDIR(st.st_mode)) {
			safeStrcat(out, cand, outSize);
			return 0;
		}
	}

	// Final fallback: CWD-based even if not verified
	if (cwd[0]) {
		safeStrcat(out, cwd, outSize);
		safeStrcat(out, "/", outSize);
		safeStrcat(out, gd, outSize);
		return 0;
	}
	return -1;
}

// ─── crash handler ──────────────────────────────────────────────────────────

static void crashHandler(int sig, siginfo_t *info, void *ucontext) {
	if (s_inCrash) _exit(1);
	s_inCrash = 1;

	// pick the first writable target (gamedir → Download → app-private);
	// mirror all output to logcat via writeStr() regardless.
	int fd = openCrashLogAppend();
	if (fd < 0) _exit(1);

	writeStr(fd, "\n=== CRASH ===\n");

	writeStr(fd, "Signal: ");
	writeStr(fd, getSignalName(sig));
	writeStr(fd, "\n");

	// Date/time
	{
		time_t now = time(NULL);
		struct tm tm_buf;
		localtime_r(&now, &tm_buf);
		char dt[64];
		strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm_buf);
		writeStr(fd, "Time: "); writeStr(fd, dt); writeStr(fd, "\n");
	}

	// Fault address
	if (info && info->si_addr) {
		char line[64] = "Fault addr: 0x";
		char hex[20];
		safeIntToHex(hex, (unsigned long)info->si_addr, sizeof(hex));
		safeStrcat(line, hex, sizeof(line));
		safeStrcat(line, "\n", sizeof(line));
		writeStr(fd, line);
	}

	// PID/TID
	{
		char line[64] = "PID: ";
		char num[16];
		safeIntToStr(num, getpid(), sizeof(num));
		safeStrcat(line, num, sizeof(line));
		safeStrcat(line, " TID: ", sizeof(line));
		safeIntToStr(num, gettid(), sizeof(num));
		safeStrcat(line, num, sizeof(line));
		safeStrcat(line, "\n", sizeof(line));
		writeStr(fd, line);
	}

	// Registers + extract key values
	unsigned long x30v = 0, x16v = 0;
#if defined(__aarch64__)
	mcontext_t *mctx = ucontext ? &((ucontext_t *)ucontext)->uc_mcontext : NULL;
	if (mctx) {
		x30v = mctx->regs[30];
		x16v = mctx->regs[16];
	}
#elif defined(__arm__)
	mcontext_t *mctx = ucontext ? &((ucontext_t *)ucontext)->uc_mcontext : NULL;
	if (mctx) {
		x30v = mctx->arm_lr;
	}
#elif defined(__x86_64__)
	mcontext_t *mctx = ucontext ? &((ucontext_t *)ucontext)->uc_mcontext : NULL;
#endif

	// Unwind backtrace
	void *frames[64];
	int frameCount = getBacktrace(frames, 64, ucontext);

	writeStr(fd, "\n--- Backtrace ---\n");

	for (int i = 0; i < frameCount; i++) {
		char line[1024];
		line[0] = '\0';

		safeStrcat(line, "  #", sizeof(line));
		char num[16];
		safeIntToStr(num, i, sizeof(num));
		safeStrcat(line, num, sizeof(line));
		safeStrcat(line, " pc ", sizeof(line));

		char hex[20];
		safeIntToHex(hex, (unsigned long)frames[i], sizeof(hex));
		safeStrcat(line, hex, sizeof(line));

		// Enhanced resolution
		char resolved[512];
		resolved[0] = '\0';
		resolveAddressEnhanced(resolved, sizeof(resolved), frames[i], fd);

		if (resolved[0]) {
			safeStrcat(line, " ", sizeof(line));
			safeStrcat(line, resolved, sizeof(line));
		}
		safeStrcat(line, "\n", sizeof(line));
		writeStr(fd, line);
	}

	// Register dump
	if (mctx) {
		writeStr(fd, "\n--- Registers ---\n");
#if defined(__aarch64__)
		{
			const char *regNames[] = {
				"x0","x1","x2","x3","x4","x5","x6","x7",
				"x8","x9","x10","x11","x12","x13","x14","x15",
				"x16","x17","x18","x19","x20","x21","x22","x23",
				"x24","x25","x26","x27","x28","x29","x30","sp",
				"pc","pstate"
			};
			for (int i = 0; i < 34; i++) {
				char line[64] = "  ";
				safeStrcat(line, regNames[i], sizeof(line));
				safeStrcat(line, " = 0x", sizeof(line));
				char hex[20];
				safeIntToHex(hex, mctx->regs[i], sizeof(hex));
				safeStrcat(line, hex, sizeof(line));
				safeStrcat(line, "\n", sizeof(line));
				writeStr(fd, line);
			}
		}
#elif defined(__arm__)
		{
			const char *regNames[] = {
				"r0","r1","r2","r3","r4","r5","r6","r7",
				"r8","r9","r10","r11","r12","sp","lr","pc","cpsr"
			};
			unsigned long regVals[] = {
				mctx->arm_r0, mctx->arm_r1, mctx->arm_r2, mctx->arm_r3,
				mctx->arm_r4, mctx->arm_r5, mctx->arm_r6, mctx->arm_r7,
				mctx->arm_r8, mctx->arm_r9, mctx->arm_r10, mctx->arm_fp,
				mctx->arm_ip, mctx->arm_sp, mctx->arm_lr, mctx->arm_pc,
				mctx->arm_cpsr
			};
			for (int i = 0; i < 17; i++) {
				char line[64] = "  ";
				safeStrcat(line, regNames[i], sizeof(line));
				safeStrcat(line, " = 0x", sizeof(line));
				char hex[20];
				safeIntToHex(hex, regVals[i], sizeof(hex));
				safeStrcat(line, hex, sizeof(line));
				safeStrcat(line, "\n", sizeof(line));
				writeStr(fd, line);
			}
		}
#elif defined(__x86_64__)
		{
			const char *regNames[] = {
				"rax","rbx","rcx","rdx","rdi","rsi","rbp","rsp",
				"r8","r9","r10","r11","r12","r13","r14","r15",
				"rip","eflags"
			};
			unsigned long regVals[] = {
				mctx->gregs[REG_RAX], mctx->gregs[REG_RBX],
				mctx->gregs[REG_RCX], mctx->gregs[REG_RDX],
				mctx->gregs[REG_RDI], mctx->gregs[REG_RSI],
				mctx->gregs[REG_RBP], mctx->gregs[REG_RSP],
				mctx->gregs[REG_R8], mctx->gregs[REG_R9],
				mctx->gregs[REG_R10], mctx->gregs[REG_R11],
				mctx->gregs[REG_R12], mctx->gregs[REG_R13],
				mctx->gregs[REG_R14], mctx->gregs[REG_R15],
				mctx->gregs[REG_RIP], mctx->gregs[REG_EFL]
			};
			for (int i = 0; i < 18; i++) {
				char line[64] = "  ";
				safeStrcat(line, regNames[i], sizeof(line));
				safeStrcat(line, " = 0x", sizeof(line));
				char hex[20];
				safeIntToHex(hex, regVals[i], sizeof(hex));
				safeStrcat(line, hex, sizeof(line));
				safeStrcat(line, "\n", sizeof(line));
				writeStr(fd, line);
			}
		}
#endif
	}

	dumpMaps(fd, (unsigned long)info->si_addr, x30v, x16v);

	writeStr(fd, "=== END CRASH ===\n");
	close(fd);

	_exit(1);
}

// ─── init ───────────────────────────────────────────────────────────────────

static struct sigaction s_oldHandlers[32];

static char s_headerPath[256] = {0};

static void CrashHandler_WriteHeader(void) {
	// Only (re)initialize when the target path changed (new process run or
	// gamedir switch). Deleting the existing file first guarantees crash.log
	// always starts brand-new — stale INIT blocks from older builds or
	// previous runs can never accumulate on top of each other.
	if (s_headerPath[0] && strcmp(s_headerPath, s_crashLogPath) == 0) return;

	// Ensure parent directory exists and open through the writable fallback
	// chain (gamedir -> Download -> app-private files dir).
	int fd = openCrashLogTrunc();
	if (fd < 0) return;

	s_headerPath[0] = '\0';
	safeStrcat(s_headerPath, s_crashLogPath, sizeof(s_headerPath));

	if (writeStr(fd, "=== CS16Client INIT ===\n") < 0) {
		close(fd);
		return;
	}

	// Date/time
	{
		time_t now = time(NULL);
		char dt[64];
		int n = strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", localtime(&now));
		writeStr(fd, "Time: "); if (n) write(fd, dt, n); writeStr(fd, "\n");
	}

	// Device info
	{
		char prop[256];
		writeStr(fd, "\n--- Device ---\n");
		if (__system_property_get("ro.product.brand", prop) > 0) {
			writeStr(fd, "Brand: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
		if (__system_property_get("ro.product.model", prop) > 0) {
			writeStr(fd, "Model: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
		if (__system_property_get("ro.product.device", prop) > 0) {
			writeStr(fd, "Device: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
		if (__system_property_get("ro.product.board", prop) > 0) {
			writeStr(fd, "Board: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
		if (__system_property_get("ro.hardware.chipname", prop) > 0) {
			writeStr(fd, "SoC: "); writeStr(fd, prop); writeStr(fd, "\n");
		} else if (__system_property_get("ro.hardware", prop) > 0) {
			writeStr(fd, "Hardware: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
		if (__system_property_get("ro.product.cpu.abilist", prop) > 0) {
			writeStr(fd, "CPU ABI: "); writeStr(fd, prop); writeStr(fd, "\n");
		} else if (__system_property_get("ro.product.cpu.abi", prop) > 0) {
			writeStr(fd, "CPU ABI: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
		if (__system_property_get("ro.build.version.release", prop) > 0) {
			writeStr(fd, "Android: "); writeStr(fd, prop);
			if (__system_property_get("ro.build.version.sdk", prop) > 0) {
				writeStr(fd, " (SDK "); writeStr(fd, prop); writeStr(fd, ")");
			}
			writeStr(fd, "\n");
		}
		if (__system_property_get("ro.build.display.id", prop) > 0) {
			writeStr(fd, "Build: "); writeStr(fd, prop); writeStr(fd, "\n");
		}
	}

	// Versions
	if (s_engineVersion[0]) {
		writeStr(fd, "Engine: "); writeStr(fd, s_engineVersion); writeStr(fd, "\n");
	}
	if (s_patcherVersion[0]) {
		writeStr(fd, "Patcher: "); writeStr(fd, s_patcherVersion); writeStr(fd, "\n");
	}

	writeStr(fd, "\n--- Running (no crash) ---\n");
	close(fd);
}

void CrashHandler_Init(void) {
	// Set up an alternate signal stack. SIGSTKSZ (8 KB) is far too small for
	// this handler: it parses /proc/self/maps, walks the frame chain and
	// mmap-reads ELF symtabs per frame, so one SIGSEGV on that stack would
	// overflow and kill the handler before any log is written (both arm32 and
	// arm64 showed this as a missing crash.log).
	static char s_signalStack[256 * 1024];
	stack_t ss;
	memset(&ss, 0, sizeof(ss));
	ss.ss_sp = s_signalStack;
	ss.ss_size = sizeof(s_signalStack);
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);

	CrashHandler_Install();
}

void CrashHandler_Install(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crashHandler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);

	int sigs[] = { SIGILL, SIGSEGV, SIGBUS, SIGABRT, SIGFPE };
	for (int i = 0; i < 5; i++) {
		sigaction(sigs[i], &sa, &s_oldHandlers[sigs[i]]);
	}
}

void CrashHandler_SetGameDir(const char *gamedir) {
	s_crashLogPath[0] = '\0';
	if (resolveGameDir(gamedir, s_crashLogPath, sizeof(s_crashLogPath)) == 0) {
		safeStrcat(s_crashLogPath, "/crash.log", sizeof(s_crashLogPath));
	}

	// Write the header once we know the final path
	CrashHandler_WriteHeader();
}

void CrashHandler_SetEngineVersion(const char *ver) {
	s_engineVersion[0] = '\0';
	if (ver && ver[0]) safeStrcat(s_engineVersion, ver, sizeof(s_engineVersion));
}

void CrashHandler_SetPatcherVersion(const char *ver) {
	s_patcherVersion[0] = '\0';
	if (ver && ver[0]) safeStrcat(s_patcherVersion, ver, sizeof(s_patcherVersion));
}

#endif // __ANDROID__
