// rr_netip.cpp - Ring Racers E: local IP addresses for the "My IP Addresses" button.
// System headers only (no game headers). Lists the IPv4 addresses of adapters that are up: LAN, Wi-Fi
// and LAN-style VPNs (ZeroTier, Hamachi, Radmin, Tailscale). NEVER shows a public IP on a regular adapter
// (e.g. a modem in bridge mode): it is counted in *hidden instead. Loopback and 169.254 (no network) are skipped.
// Windows: GetAdaptersAddresses is loaded with LoadLibrary (no need to link iphlpapi).
#include <stdio.h>
#include <string.h>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "rr_nete.h"

namespace {

bool has(const std::string &hay, const char *needle)
{
	std::string a = hay, b = needle;
	for (auto &c : a) c = (char)tolower((unsigned char)c);
	for (auto &c : b) c = (char)tolower((unsigned char)c);
	return a.find(b) != std::string::npos;
}

// "ZeroTier", "Hamachi"... for a known LAN-style VPN, otherwise ""
const char *vpn_name(const std::string &name, const std::string &desc)
{
	if (has(name, "zerotier") || has(desc, "zerotier") || name.compare(0, 2, "zt") == 0) return "ZeroTier";
	if (has(name, "hamachi") || has(desc, "hamachi") || name == "ham0") return "Hamachi";
	if (has(name, "radmin") || has(desc, "radmin")) return "Radmin VPN";
	if (has(name, "tailscale") || has(desc, "tailscale")) return "Tailscale";
	return "";
}

// virtual machine / WSL adapters: private IP, but useless for playing
bool is_vm(const std::string &name, const std::string &desc)
{
	static const char *const v[] = {"vethernet", "virtualbox", "vmware", "hyper-v", "docker", "wsl", "vbox", "virbr", "veth", "bluetooth", nullptr};
	for (int i = 0; v[i]; i++)
		if (has(name, v[i]) || has(desc, v[i]))
			return true;
	return false;
}

bool is_private(unsigned a, unsigned b)
{
	return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127);
}

struct Out
{
	char *buf; int size; int used = 0; int count = 0; int hidden = 0;
	void add(const char *label, bool vpn, const unsigned char ip[4], const char *port)
	{
		unsigned a = ip[0], b = ip[1];
		if (a == 127 || a == 0 || (a == 169 && b == 254))
			return;
		if (!vpn && !is_private(a, b)) // regular adapter with a public IP: NEVER shown
		{
			hidden++;
			return;
		}
		char line[96];
		snprintf(line, sizeof(line), " %s: %u.%u.%u.%u:%s\n", label, a, b, ip[2], ip[3], port);
		int n = (int)strlen(line);
		if (used + n + 1 >= size || count >= 8)
			return;
		memcpy(buf + used, line, n + 1);
		used += n;
		count++;
	}
};

} // namespace

extern "C" int RRNetIP_List(char *out, int outsz, const char *port, int *hidden)
{
	if (!out || outsz <= 0)
		return 0;
	out[0] = '\0';
	if (!port)
		port = "5029";
	Out o{out, outsz};

#ifdef _WIN32
	typedef ULONG(WINAPI * gaa_t)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
	HMODULE dll = LoadLibraryA("iphlpapi.dll");
	gaa_t gaa = dll ? (gaa_t)(void *)GetProcAddress(dll, "GetAdaptersAddresses") : nullptr;
	if (gaa)
	{
		ULONG len = 32 * 1024;
		std::string mem;
		ULONG r = ERROR_BUFFER_OVERFLOW;
		for (int tries = 0; tries < 3 && r == ERROR_BUFFER_OVERFLOW; tries++)
		{
			mem.resize(len);
			r = gaa(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
				(PIP_ADAPTER_ADDRESSES)&mem[0], &len);
		}
		if (r == NO_ERROR)
		{
			for (auto *ad = (PIP_ADAPTER_ADDRESSES)&mem[0]; ad; ad = ad->Next)
			{
				if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
					continue;
				char fn[128] = "", ds[128] = "";
				if (ad->FriendlyName) WideCharToMultiByte(CP_ACP, 0, ad->FriendlyName, -1, fn, sizeof(fn), nullptr, nullptr);
				if (ad->Description) WideCharToMultiByte(CP_ACP, 0, ad->Description, -1, ds, sizeof(ds), nullptr, nullptr);
				std::string name = fn, desc = ds;
				const char *vpn = vpn_name(name, desc);
				if (!*vpn && is_vm(name, desc))
					continue;
				for (auto *ua = ad->FirstUnicastAddress; ua; ua = ua->Next)
				{
					if (!ua->Address.lpSockaddr || ua->Address.lpSockaddr->sa_family != AF_INET)
						continue;
					const unsigned char *ip = (const unsigned char *)&((sockaddr_in *)ua->Address.lpSockaddr)->sin_addr;
					// Ethernet = "LAN", Wi-Fi = "Wi-Fi"; anything else (generic VPN, tunnel...) = its name, shortened
					std::string lab = *vpn ? vpn : (ad->IfType == IF_TYPE_IEEE80211 ? "Wi-Fi" : (ad->IfType == IF_TYPE_ETHERNET_CSMACD ? "LAN" : name.substr(0, 16)));
					if (lab.empty())
						lab = "LAN";
					o.add(lab.c_str(), *vpn != 0, ip, port);
				}
			}
		}
	}
	if (dll)
		FreeLibrary(dll);
#else
	struct ifaddrs *ifa = nullptr;
	if (getifaddrs(&ifa) == 0)
	{
		for (auto *it = ifa; it; it = it->ifa_next)
		{
			if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK))
				continue;
			std::string name = it->ifa_name ? it->ifa_name : "";
			const char *vpn = vpn_name(name, "");
			if (!*vpn && is_vm(name, ""))
				continue;
			const unsigned char *ip = (const unsigned char *)&((sockaddr_in *)it->ifa_addr)->sin_addr;
			o.add(*vpn ? vpn : (name.compare(0, 2, "wl") == 0 ? "Wi-Fi" : "LAN"), *vpn != 0, ip, port);
		}
		freeifaddrs(ifa);
	}
#endif

	if (hidden)
		*hidden = o.hidden;
	return o.count;
}
