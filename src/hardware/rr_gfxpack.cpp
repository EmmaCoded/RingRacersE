// rr_gfxpack.cpp - Ring Racers E: ANGLE and Mesa DLLs embedded INSIDE the .exe (Windows, 64-bit).
// src/hardware/CMakeLists.txt gzips RRE_EMBED_DIR/angle/*.dll and RRE_EMBED_DIR/mesa/*.dll at configure time and
// generates rre_pack.inc (.incbin of each file + a table). The first time DirectX 11, Vulkan or DirectX 12 is used,
// the DLLs of that group are written to %LOCALAPPDATA%\RingRacersE\<group>-<id>\ and loaded from there.
// <id> comes from the CRC32 of the files, so a new version goes to a new folder and never fights an old copy.
// Without embedded files (RRE_PACK not defined) everything returns 0 and only the folders next to the exe count.
#include "rr_gfxpack.h"

extern "C" void CONS_Printf(const char *fmt, ...);

#if defined(_WIN32) && defined(_WIN64) && defined(RRE_PACK)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <zlib.h>

struct RRPackFile
{
	const char *group;
	const char *name;
	const unsigned char *start;
	const unsigned char *end;
};

#include "rre_pack.inc" // defines rr_pack_files[] and rr_pack_count

// gzip trailer: CRC32 and the uncompressed size (both little endian, last 8 bytes)
static unsigned rd32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static const RRPackFile *find_file(const char *group, const char *name)
{
	for (int i = 0; i < rr_pack_count; i++)
		if (!strcmp(rr_pack_files[i].group, group) && !lstrcmpiA(rr_pack_files[i].name, name))
			return &rr_pack_files[i];
	return NULL;
}

int RRPack_Has(const char *group, const char *name)
{
	return find_file(group, name) != NULL;
}

static unsigned long long file_size(const char *path)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return ~0ULL;
	return ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

// Inflates one gzip stream to a file (written as .tmp and renamed, so a half-written DLL is never left behind)
static int write_file(const RRPackFile *f, const char *path)
{
	char tmp[MAX_PATH + 16];
	wsprintfA(tmp, "%s.tmp", path);
	HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	z_stream z;
	memset(&z, 0, sizeof z);
	int ok = inflateInit2(&z, 16 + MAX_WBITS) == Z_OK;
	static unsigned char buf[1 << 20];
	z.next_in = (Bytef *)f->start;
	z.avail_in = (uInt)(f->end - f->start);
	int r = Z_OK;
	while (ok && r != Z_STREAM_END)
	{
		z.next_out = buf;
		z.avail_out = sizeof buf;
		r = inflate(&z, Z_NO_FLUSH);
		if (r != Z_OK && r != Z_STREAM_END)
			ok = 0;
		DWORD n = (DWORD)(sizeof buf - z.avail_out), w = 0;
		if (ok && n && (!WriteFile(h, buf, n, &w, NULL) || w != n))
			ok = 0;
	}
	inflateEnd(&z);
	CloseHandle(h);
	if (ok && !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
		ok = 0;
	if (!ok)
		DeleteFileA(tmp);
	return ok;
}

int RRPack_Extract(const char *group, char *out, int n)
{
	unsigned id = 0;
	int count = 0;
	for (int i = 0; i < rr_pack_count; i++)
		if (!strcmp(rr_pack_files[i].group, group))
		{
			id = id * 31 + rd32(rr_pack_files[i].end - 8);
			count++;
		}
	char base[MAX_PATH];
	DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
	if (!count || !len || len >= MAX_PATH - 64)
		return 0;
	char dir[MAX_PATH];
	wsprintfA(dir, "%s\\RingRacersE", base);
	CreateDirectoryA(dir, NULL);
	wsprintfA(dir, "%s\\RingRacersE\\%s-%08x", base, group, id);
	CreateDirectoryA(dir, NULL);
	for (int i = 0; i < rr_pack_count; i++)
	{
		const RRPackFile *f = &rr_pack_files[i];
		if (strcmp(f->group, group))
			continue;
		char path[MAX_PATH + 64];
		wsprintfA(path, "%s\\%s", dir, f->name);
		if (file_size(path) == rd32(f->end - 4))
			continue; // already written earlier
		if (!write_file(f, path))
		{
			CONS_Printf("[GFXAPI] could not write %s\n", path);
			return 0;
		}
		CONS_Printf("[GFXAPI] embedded %s written to %s\n", f->name, dir);
	}
	lstrcpynA(out, dir, n);
	return 1;
}

#else
int RRPack_Has(const char *, const char *) { return 0; }
int RRPack_Extract(const char *, char *, int) { return 0; }
#endif
