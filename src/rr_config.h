// rr_config.h - Ring Racers E: optional developer/test options, plus the Low-End Mode switch.
// Test options are read from RRCFG_DIR/rr_config.txt, one option per line:
//     key = value      # comment
// If a key is missing there, the legacy single-option file RRCFG_DIR/rr_<key> is checked instead.
// Without rr_config.txt every option keeps its default (the normal case for players).
// Header only (static inline), so no build changes are needed.
#ifndef RR_CONFIG_H
#define RR_CONFIG_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define RRCFG_DIR "devconfig"

// Copies the value of 'key' into out. Returns 1 if it exists (in rr_config.txt or a legacy file).
static inline int RRCfg_Str(const char *key, char *out, size_t n)
{
	static char buf[4096];
	static int loaded = 0;
	if (!loaded)
	{
		loaded = 1;
		FILE *f = fopen(RRCFG_DIR "/rr_config.txt", "rb");
		if (f) { size_t r = fread(buf, 1, sizeof(buf) - 1, f); buf[r] = 0; fclose(f); printf("[CFG] rr_config.txt: %u bytes\n", (unsigned)r); }
		else { buf[0] = 0; } // no rr_config.txt = defaults, silently
	}
	if (n) out[0] = 0;
	const size_t lc = strlen(key);
	for (const char *p = buf; *p; )
	{
		const char *eol = p; while (*eol && *eol != '\n') eol++;
		const char *s = p; while (s < eol && (*s == ' ' || *s == '\t')) s++;
		if ((size_t)(eol - s) > lc && !strncasecmp(s, key, lc))
		{
			const char *q = s + lc; while (q < eol && (*q == ' ' || *q == '\t')) q++;
			if (q < eol && (*q == '=' || *q == ':'))
			{
				q++; while (q < eol && (*q == ' ' || *q == '\t')) q++;
				const char *e = q; while (e < eol && *e != '#' && *e != '\r') e++;
				while (e > q && (e[-1] == ' ' || e[-1] == '\t')) e--;
				size_t len = (size_t)(e - q); if (n && len >= n) len = n - 1;
				if (n) { memcpy(out, q, len); out[len] = 0; }
				return 1;
			}
		}
		p = *eol ? eol + 1 : eol;
	}
	// legacy single-option file rr_<key>
	char path[128];
	snprintf(path, sizeof path, RRCFG_DIR "/rr_%s", key);
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	size_t r = n ? fread(out, 1, n - 1, f) : 0;
	fclose(f);
	if (n) { out[r] = 0; while (r && isspace((unsigned char)out[r - 1])) out[--r] = 0; }
	return 1;
}

// Switch: on if present and not 0/off/no (an empty legacy file counts as on).
static inline int RRCfg_OnDef(const char *key, int def)
{
	char v[32];
	if (!RRCfg_Str(key, v, sizeof v)) return def;
	return !(v[0] == '0' || !strcasecmp(v, "off") || !strcasecmp(v, "no"));
}
static inline int RRCfg_On(const char *key)
{
	char v[32];
	if (!RRCfg_Str(key, v, sizeof v)) return 0;
	return !(v[0] == '0' || !strcasecmp(v, "off") || !strcasecmp(v, "no"));
}

// Number; 'def' if missing or not a number.
static inline int RRCfg_Int(const char *key, int def)
{
	char v[32]; int x;
	if (!RRCfg_Str(key, v, sizeof v) || sscanf(v, "%d", &x) != 1) return def;
	return x;
}

// [TAG] diagnostic lines go to the log only (I_OutputMsg). With CONS_Printf they would also show
// up as console text on top of the screen during a race.
#define RR_DIAGPRINT I_OutputMsg

// LOW-END MODE: lighter game logic for slow machines (sleeping rings, bot AI every 2 tics, distant
// objects think less often, fewer particles...). Chosen in Video Options > Advanced (cv_rrlowend) and
// latched into rr_lowend when each course loads. It changes the simulation, so in netgames everyone
// uses the server's value (rrnetlowend, see rr_nete.c) and replays keep the value they were recorded with.
#define RR_LOWEND_CODE 1
#ifdef __cplusplus
extern "C" int rr_lowend;
#else
extern int rr_lowend;
#endif
#define RR_LOWEND (rr_lowend)

#endif
