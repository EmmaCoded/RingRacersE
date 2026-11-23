// Ring Racers E: its own config file (ringconfigE.cfg), kept in sync with the original Ring Racers config.
//
// Why: E has options the original game doesn't know (gfxapi...). If both games shared one file, the original
// could choke on them. Each game keeps its own file, and when E starts the NON-graphics options are made
// equal in both. Graphics options (renderer, gfxapi, resolution, gamma...) stay separate.
//
// Rule: whichever file was modified last wins. If it was the original, its non-graphics options go to E.
// If it was E, its options go to the original, but only those the original already has (E-only options
// are never added to the original file).

#include "rr_cfgsync.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <fstream>
#include <string>
#include <vector>

extern "C" void CONS_Printf(const char *fmt, ...);

namespace
{

struct Line
{
	std::string raw;
	std::string key; // empty = comment/blank line
};

bool ends_with(const std::string &s, const char *suffix)
{
	const size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Graphics options: NOT synced.
bool is_graphics_key(const std::string &k)
{
	static const char *const kExact[] = {
		"renderer", "gfxapi", "fullscreen", "vid_wait", "scr_depth", "scr_height", "scr_width", "scr_scale",
		"scr_x", "scr_y", "scr_effect", "parallelsoftware", "drawdist", "drawdist_precip", "fpscap", "showfps",
		"skybox", "shadow", "homremoval", "ffloorclip", "rrlowend", "rrapirestart", "rrborderless", "rrdisplaymode", nullptr}; // rrlowend/rrapirestart/rrborderless: E only
	for (int i = 0; kExact[i]; i++)
		if (k == kExact[i])
			return true;
	if (k.compare(0, 3, "gr_") == 0)
		return true;
	// screen color settings: gamma, hue and saturation (rgamma, ghue, csaturation...)
	return ends_with(k, "gamma") || ends_with(k, "hue") || ends_with(k, "saturation");
}

// First token; for "setcontrol" also the second one (there is one line per control).
std::string key_of(const std::string &line)
{
	size_t i = 0;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
		i++;
	if (i >= line.size() || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
		return std::string();
	size_t j = i;
	while (j < line.size() && line[j] != ' ' && line[j] != '\t')
		j++;
	std::string key = line.substr(i, j - i);
	if (key.compare(0, 10, "setcontrol") == 0)
	{
		size_t a = j;
		while (a < line.size() && (line[a] == ' ' || line[a] == '\t'))
			a++;
		size_t b = a;
		if (b < line.size() && line[b] == '"')
		{
			b++;
			while (b < line.size() && line[b] != '"')
				b++;
			if (b < line.size())
				b++;
		}
		else
			while (b < line.size() && line[b] != ' ' && line[b] != '\t')
				b++;
		key += " " + line.substr(a, b - a);
	}
	return key;
}

bool read_lines(const std::string &path, std::vector<Line> &out)
{
	std::ifstream f(path.c_str(), std::ios::binary);
	if (!f)
		return false;
	std::string s;
	while (std::getline(f, s))
	{
		if (!s.empty() && s.back() == '\r')
			s.pop_back();
		out.push_back({s, key_of(s)});
	}
	return true;
}

bool write_lines(const std::string &path, const std::vector<Line> &lines)
{
	const std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		for (const Line &l : lines)
			f << l.raw << "\n";
		if (!f)
			return false;
	}
	remove(path.c_str()); // rename doesn't overwrite on Windows
	return rename(tmp.c_str(), path.c_str()) == 0;
}

int find_key(const std::vector<Line> &v, const std::string &key)
{
	for (size_t i = 0; i < v.size(); i++)
		if (!v[i].key.empty() && v[i].key == key)
			return (int)i;
	return -1;
}

time_t mtime_of(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

} // namespace

extern "C" void RR_CfgSync(char *configfile, unsigned long size)
{
	static const char kOrig[] = "ringconfig.cfg";
	static const char kE[] = "ringconfigE.cfg";

	std::string orig = configfile;
	const size_t pos = orig.rfind(kOrig);
	if (pos == std::string::npos || pos + strlen(kOrig) != orig.size())
		return; // different path (dev mode, -config...): leave it alone
	const std::string epath = orig.substr(0, pos) + kE;

	std::vector<Line> lo, le;
	const bool has_orig = read_lines(orig, lo);
	const bool has_e = read_lines(epath, le);

	if (has_orig)
	{
		if (!has_e)
		{
			// First run: E starts with the original's non-graphics options
			for (const Line &l : lo)
				if (l.key.empty() || !is_graphics_key(l.key))
					le.push_back(l);
			if (write_lines(epath, le))
				CONS_Printf("[CFG-E] ringconfigE.cfg created from ringconfig.cfg (without graphics options)\n");
		}
		else if (mtime_of(orig) > mtime_of(epath))
		{
			// The original was saved last: its non-graphics options go to E
			int changed = 0;
			for (const Line &l : lo)
			{
				if (l.key.empty() || is_graphics_key(l.key))
					continue;
				const int i = find_key(le, l.key);
				if (i < 0)
				{
					le.push_back(l);
					changed++;
				}
				else if (le[i].raw != l.raw)
				{
					le[i].raw = l.raw;
					changed++;
				}
			}
			if (changed && write_lines(epath, le))
				CONS_Printf("[CFG-E] %d options copied from the original to ringconfigE.cfg\n", changed);
		}
		else
		{
			// E was saved last: its non-graphics options go to the original (only those the original already has)
			int changed = 0;
			for (const Line &l : le)
			{
				if (l.key.empty() || is_graphics_key(l.key))
					continue;
				const int i = find_key(lo, l.key);
				if (i >= 0 && lo[i].raw != l.raw)
				{
					lo[i].raw = l.raw;
					changed++;
				}
			}
			if (changed && write_lines(orig, lo))
				CONS_Printf("[CFG-E] %d options copied from E to ringconfig.cfg\n", changed);
		}
	}

	snprintf(configfile, size, "%s", epath.c_str());
}

