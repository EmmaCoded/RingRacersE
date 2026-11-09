// rr_nete.h - Ring Racers E: netgames restricted to Ring Racers E (direct IP connection),
// the "My IP Addresses" button and Low-End Mode synced over the network.
// Implementation in rr_nete.c and rr_netip.cpp.
#ifndef RR_NETE_H
#define RR_NETE_H


#ifdef __cplusplus
extern "C" {
#endif

struct serverinfo_pak;

// --- client ---
void RRNet_CheckServerInfo(const struct serverinfo_pak *info); // called when the joined server's info arrives
int RRNet_WrongServer(void);       // 1 if that server is not Ring Racers E (or runs another version)
void RRNet_ShowWrongServer(void);  // "Ring Racers E only" message (and clears the flag)
void RRNet_ClientFoundServer(int serverlowend); // prepares the "different Low-End Mode" notice

// --- server ---
void RRNet_SetNodeLowEnd(int node, int lowend); // the client on that node uses Low-End Mode
void RRNet_BeforeAddPlayer(int node, int playernum); // before XD_ADDPLAYER: mark as waiting if needed
void RRNet_ServerStart(void);      // SV_SpawnServer (online): the game starts with the host's Low-End setting

// --- both ---
void RRNet_Update(void);           // every NetUpdate
void RRNet_LowEndNetChanged(void); // onchange of the rrnetlowend netvar (runs on every machine)
void RRNet_ShowMyIPs(int choice);  // "My IP Addresses" button (Host Game menu) and Y button (Online menu, host pause menu)
void RRNet_DrawMyIPsHint(INT32 x, INT32 y, boolean strip); // "<Y> My IP Addresses" hint in the bottom left corner
boolean RRNet_MyIPsInput(UINT8 pid);              // Y -> IP message box (true if used)
extern int rrnet_ipmsg_open;                      // the open message box is the IP one (also closes with Y)

// rr_netip.cpp: writes the useful local IPs (LAN, Wi-Fi, ZeroTier, Hamachi...) with the port into out,
// one per line. NEVER the public IP. Returns how many were written; *hidden = public IPs that were hidden.
int RRNetIP_List(char *out, int outsz, const char *port, int *hidden);

#ifdef __cplusplus
}
#endif

#endif // RR_NETE_H
