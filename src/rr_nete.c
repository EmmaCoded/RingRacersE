// rr_nete.c - Ring Racers E. See rr_nete.h.
//
// RING RACERS E ONLY: an E client sends application = RR_NETAPP ("RingRacersE"); an E server rejects
// anyone else with a "Ring Racers E only" message. An E server advertises application "RingRacers" plus
// the SV_RRE bit, so original Ring Racers clients get far enough to ask to join and receive that message.
// An E client requires the SV_RRE bit.
// No public server list: only "Direct Join" by IP (the list would be the original game's, incompatible).
//
// LOW-END MODE IN NETGAMES (it changes the simulation, so every machine must use the same value):
//  - The rrnetlowend netvar is the game's Low-End setting. Everyone reads it in P_LoadLevel. The server
//    only changes it OUTSIDE a course (intermission/voting), so a whole course always uses one value.
//  - The server wants Low-End if the host has it enabled OR any connected player uses Low-End.
//  - A Low-End client joining a non-Low-End game spectates (bit in the rrnetlowwait netvar, honored by
//    K_CheckSpectateStatus identically on every machine) until the round ends and the server switches
//    to Low-End. If that player leaves, the host's setting comes back when the round ends.
//  - A regular client joining a Low-End game plays in Low-End Mode (with a notice).
#include "doomdef.h"
#include "doomstat.h"
#include "d_clisrv.h"
#include "d_net.h"
#include "d_netcmd.h"
#include "command.h"
#include "console.h"
#include "g_game.h"
#include "k_menu.h"
#include "s_sound.h"
#include "hu_stuff.h" // chat_on
#include "screen.h"
#include "rr_nete.h"
#include "k_hud.h" // K_DrawGameControl
#include "v_video.h"


static UINT8 s_nodelowend[MAXNETNODES]; // server: the client on that node asked for Low-End
static int s_wrongserver;   // client: 1 = original Ring Racers, 2 = another game/mod, 3 = another E version
static int s_joinmsg;       // client: 1 = I use Low-End and the host doesn't, 2 = the other way around
static int s_sentlow = -1;  // server: rrnetlowend value already requested (waiting for the XD)
static int s_sentwait = -1; // server: same for rrnetlowwait

// ------------------------------------------------------------------ client

void RRNet_CheckServerInfo(const serverinfo_pak *info)
{
	if (strcmp(info->application, SRB2APPLICATION))
		s_wrongserver = 2;
	else if (!(info->kartvars & SV_RRE))
		s_wrongserver = 1;
	else if (info->version != VERSION || info->subversion != RR_NETINFO_SUBVERSION)
		s_wrongserver = 3;
	else
		s_wrongserver = 0;
}

int RRNet_WrongServer(void)
{
	return s_wrongserver != 0;
}

void RRNet_ShowWrongServer(void)
{
	const char *txt;
	if (s_wrongserver == 1)
		txt = "That is an original Ring Racers game.\n\nRing Racers E can only join\nRing Racers E games (its files and\ngame logic differ).\n";
	else if (s_wrongserver == 3)
		txt = "That Ring Racers E game uses a\ndifferent version.\n\nBoth players need the same\nRing Racers E version.\n";
	else
		txt = "That is not a Ring Racers E game.\n\nRing Racers E can only join\nRing Racers E games.\n";
	CONS_Printf("[RRNET] incompatible server (type %d): not Ring Racers E of the same version\n", s_wrongserver);
	s_wrongserver = 0;
	M_StartMessage("Server Connection Failure", M_GetText(txt), NULL, MM_NOTHING, NULL, "Back to Menu");
}

void RRNet_ClientFoundServer(int serverlowend)
{
	if (cv_rrlowend.value && !serverlowend)
		s_joinmsg = 1;
	else if (!cv_rrlowend.value && serverlowend)
		s_joinmsg = 2;
	else
		s_joinmsg = 0;
}

static void RRNet_ClientUpdate(void)
{
	if (!netgame)
	{
		s_joinmsg = 0;
		return;
	}
	if (!s_joinmsg || !addedtogame || menuactive || chat_on)
		return;
	if (gamestate != GS_LEVEL && gamestate != GS_INTERMISSION && gamestate != GS_VOTING)
		return;

	if ((s_joinmsg == 1 && !cv_rrnetlowend.value) || (s_joinmsg == 2 && cv_rrnetlowend.value))
		M_StartControlPanel(); // the message box is only drawn with the menu open (doesn't pause online)
	if (s_joinmsg == 1 && !cv_rrnetlowend.value)
		M_StartMessage("Low-End Mode", M_GetText("The host isn't using Low-End Mode.\n\nYou'll spectate until this round\nends. Then everyone switches to\nLow-End Mode and you can race.\n"), NULL, MM_NOTHING, NULL, NULL);
	else if (s_joinmsg == 2 && cv_rrnetlowend.value)
		M_StartMessage("Low-End Mode", M_GetText("This game uses Low-End Mode,\nso you'll play with it too.\n(The host decides.)\n"), NULL, MM_NOTHING, NULL, NULL);
	s_joinmsg = 0;
}

// ------------------------------------------------------------------ server

void RRNet_SetNodeLowEnd(int node, int lowend)
{
	if (node < 0 || node >= MAXNETNODES)
		return;
	if (server && netgame && lowend && !s_nodelowend[node] && !cv_rrnetlowend.value)
		CONS_Printf("\x82" "A Low-End Mode player is joining: everyone switches to Low-End Mode when this round ends.\n");
	s_nodelowend[node] = lowend ? 1 : 0;
}

static int RRNet_PlayerLowEnd(int i)
{
	if (!playeringame[i] || players[i].bot)
		return 0;
	if (playernode[i] >= MAXNETNODES)
		return 0;
	return s_nodelowend[playernode[i]];
}

void RRNet_BeforeAddPlayer(int node, int playernum)
{
	int mask, want;
	if (!server || !netgame || playernum < 0 || playernum >= MAXPLAYERS)
		return;
	mask = cv_rrnetlowwait.value;
	if (s_sentwait >= 0)
		mask = s_sentwait; // a change is already on its way: start from it
	want = (node >= 0 && node < MAXNETNODES && s_nodelowend[node] && !cv_rrnetlowend.value);
	if (want)
		mask |= (1 << playernum);
	else
		mask &= ~(1 << playernum); // that slot belonged to a Low-End player who left
	if (mask != cv_rrnetlowwait.value || (s_sentwait >= 0 && mask != s_sentwait))
	{
		CV_SetValue(&cv_rrnetlowwait, mask);
		s_sentwait = mask;
	}
}

void RRNet_ServerStart(void)
{
	// Nobody is connected yet: set it directly (an XD would be lost before the host joins).
	boolean ng = netgame;
	memset(s_nodelowend, 0, sizeof(s_nodelowend));
	s_sentlow = s_sentwait = -1;
	netgame = false;
	CV_StealthSetValue(&cv_rrnetlowend, cv_rrlowend.value ? 1 : 0);
	CV_StealthSetValue(&cv_rrnetlowwait, 0);
	netgame = ng;
	CONS_Printf("[LOWEND] netgame %s\n", cv_rrnetlowend.value ? "with Low-End (host uses it)" : "without Low-End");
}

static void RRNet_ServerUpdate(void)
{
	int i, want = cv_rrlowend.value ? 1 : 0, mask = 0;

	for (i = 0; i < MAXPLAYERS; i++)
		if (RRNet_PlayerLowEnd(i))
			want = 1;

	// 1) the game's Low-End setting: only between rounds (never mid-course)
	if (cv_rrnetlowend.value == want)
		s_sentlow = -1;
	else if (gamestate != GS_LEVEL && s_sentlow != want)
	{
		CV_SetValue(&cv_rrnetlowend, want);
		s_sentlow = want;
	}

	// 2) who waits as a spectator: Low-End players while the game isn't Low-End
	if (!cv_rrnetlowend.value)
		for (i = 0; i < MAXPLAYERS; i++)
			if (RRNet_PlayerLowEnd(i))
				mask |= (1 << i);
	if (cv_rrnetlowwait.value == mask)
		s_sentwait = -1;
	else if (s_sentwait != mask)
	{
		CV_SetValue(&cv_rrnetlowwait, mask);
		s_sentwait = mask;
	}
}

// ------------------------------------------------------------------ both

static void Command_RRMyIP_f(void)
{
	RRNet_ShowMyIPs(0);
}

void RRNet_Update(void)
{
	static int registered = 0;
	if (!registered) // console command "rrmyip" = same message as the button
	{
		registered = 1;
		COM_AddCommand("rrmyip", Command_RRMyIP_f);
	}
	if (!netgame)
	{
		s_sentlow = s_sentwait = -1;
		s_joinmsg = 0;
		return;
	}
	if (server)
		RRNet_ServerUpdate();
	else
		RRNet_ClientUpdate();
}

void RRNet_LowEndNetChanged(void)
{
	extern int rr_lowend;
	if (!netgame)
		return;
	// Applied on the same tic on EVERY machine (XDs arrive in order), so a player joining mid-course
	// (who loads the current netvar) always matches everyone else, even if the change lands mid-course.
	rr_lowend = cv_rrnetlowend.value ? 1 : 0;
	if (client && !addedtogame) // netvars arrive all at once when joining: not a change
		return;
	CONS_Printf("\x82" "Low-End Mode %s from the next course.\n", cv_rrnetlowend.value ? "ON" : "OFF");
}

void RRNet_ShowMyIPs(int choice)
{
	extern const char *I_RR_ServerPort(void);
	static char list[1024];
	static char msg[1400];
	const char *port = I_RR_ServerPort();
	int hidden = 0, n;
	(void)choice;

	n = RRNetIP_List(list, sizeof(list), (port && *port) ? port : NULL, &hidden);
	if (n <= 0)
		snprintf(list, sizeof(list), "(No local network found.)\n");

	snprintf(msg, sizeof(msg),
		"Other Ring Racers E players can\njoin with Online > Direct Join:\n\n%s\n%s"
		"Only Ring Racers E can join.\nYour public IP is never shown.\nTo play over the internet like a LAN,\nuse ZeroTier (or similar) on all PCs.\n",
		list,
		hidden ? "(A public IP was hidden for safety.)\n" : "");
	CONS_Printf("[RRNET] My IP Addresses:\n%s", list);
	if (!menuactive)
		M_StartControlPanel(); // from the console: the message box is only visible with the menu open
	M_StartMessage("My IP Addresses", msg, NULL, MM_NOTHING, NULL, NULL);
	rrnet_ipmsg_open = 1; // this message box also closes with Y (message-box.c)
}

// "<Y> My IP Addresses" hint in the bottom left corner (Online menu and the host's pause menu).
// Not X: in menus X means "back". strip = dark strip behind it (Online menu, where the master server one was).
int rrnet_ipmsg_open = 0;
void RRNet_DrawMyIPsHint(INT32 x, INT32 y, boolean strip)
{
	if (strip)
		V_DrawFadeFill(0, y-1, BASEVIDWIDTH, 10+1, 0, 31, 5);
	K_DrawGameControl(x, y, 0, "<y_animated> My IP Addresses", 0, TINY_FONT, 0);
}

// Y pressed in that menu: opens the IP message box. true = the button was used.
boolean RRNet_MyIPsInput(UINT8 pid)
{
	if (!M_MenuButtonPressed(pid, MBT_Y))
		return false;
	M_SetMenuDelay(pid);
	RRNet_ShowMyIPs(0);
	return true;
}

