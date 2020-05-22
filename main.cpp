//--------------------------------------------//
// MCLANRelay                                 //
// License: Public Domain (www.unlicense.org) //
//--------------------------------------------//

#define MCLANRELAY_VERSION "1.0"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#define STS_NET_IMPLEMENTATION
#define STS_NET_NO_PACKETS
#define STS_NET_NO_ERRORSTRINGS
#include "sts_net.inl"

#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#define THREAD_CC WINAPI
struct Thread { typedef DWORD RET_t; typedef RET_t (WINAPI *FUNC_t)(LPVOID); Thread() : h(0) {} Thread(FUNC_t f, void* p = NULL) : h(0) { Start(f, p); } void Start(FUNC_t f, void* p = NULL) { if (h) this->~Thread(); h = CreateThread(0,0,f,p,0,0); } void Detach() { if (h) CloseHandle(h); h = 0; } ~Thread() { if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); } } private:HANDLE h;Thread(const Thread&);Thread& operator=(const Thread&);};
struct Mutex { Mutex() : h(CreateMutexA(0,0,0)) {} ~Mutex() { CloseHandle(h); } __inline void Lock() { WaitForSingleObject(h,INFINITE); } __inline void Unlock() { ReleaseMutex(h); } private:HANDLE h;Mutex(const Mutex&);Mutex& operator=(const Mutex&);};
static void sleep_ms(unsigned int ms) { Sleep(ms); }
#else
#include <pthread.h>
#define THREAD_CC
struct Thread { typedef void* RET_t; typedef RET_t (*FUNC_t)(void*); Thread() : h(0) {} Thread(FUNC_t f, void* p = NULL) : h(0) { Start(f, p); } void Start(FUNC_t f, void* p = NULL) { if (h) this->~Thread(); pthread_create(&h, NULL, f, p); } void Detach() { h = 0; } ~Thread() { if (h) { pthread_join(h, 0); h = 0; } } private:pthread_t h;Thread(const Thread&);Thread& operator=(const Thread&);};
struct Mutex { Mutex() { pthread_mutex_init(&h,0); } ~Mutex() { pthread_mutex_destroy(&h); } __inline void Lock() { pthread_mutex_lock(&h); } __inline void Unlock() { pthread_mutex_unlock(&h); } private:pthread_mutex_t h;Mutex(const Mutex&);Mutex& operator=(const Mutex&);};
static void sleep_ms(unsigned int ms)
{
	timespec req, rem;
	req.tv_sec = ms / 1000;
	req.tv_nsec = (ms % 1000) * 1000000ULL;
	while (nanosleep(&req, &rem)) req = rem;
}
#endif

#ifdef MCLANRELAY_USE_WIN32SYSTRAY
#include <shellapi.h>

static void LogMsgBox(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	char* logbuf = (char*)malloc(1+vsnprintf(NULL, 0, format, ap));
	vsprintf(logbuf, format, ap);
	va_end(ap);
	MessageBoxA(0, logbuf, "MCLANRelay Error", MB_ICONSTOP);
	free(logbuf);
}

static Mutex trayEntriesMutex;
static std::vector<const struct TrayEntry*> trayEntries;
struct TrayEntry
{
	std::string str;
	TrayEntry(const char *format, ...)
	{
		va_list ap;
		va_start(ap, format);
		str.resize(vsnprintf(NULL, 0, format, ap));
		vsprintf((char*)str.c_str(), format, ap);
		va_end(ap);
		trayEntriesMutex.Lock();
		trayEntries.push_back(this);
		trayEntriesMutex.Unlock();
	}
	~TrayEntry()
	{
		trayEntriesMutex.Lock();
		size_t i;for (i = 0; i != trayEntries.size(); i++) if (trayEntries[i] == this) break;
		trayEntries.erase(trayEntries.begin() + i);
		trayEntriesMutex.Unlock();
	}
};

static void (*ExitSystray)();
static void SetupSystray(bool serverMode, bool clientMode)
{
	static HWND systrayHwnd;
	static char title[32];
	sprintf(title, "%s%s%s%s", "MCLANRelay", (serverMode != clientMode ? " - " : ""), (serverMode != clientMode ? (serverMode ? "Server" : "Client") : ""), (serverMode != clientMode ? " Mode" : ""));
	struct Wnd
	{
		enum { IDM_NONE, IDM_EXIT };

		static void OpenMenu(HWND hWnd, bool AtCursor)
		{
			static POINT lpClickPoint;
			if (AtCursor) GetCursorPos(&lpClickPoint);
			HMENU hPopMenu = CreatePopupMenu();
			InsertMenuA(hPopMenu, 0xFFFFFFFF, MF_STRING | MF_GRAYED, IDM_NONE, title);
			InsertMenuA(hPopMenu, 0xFFFFFFFF, MF_SEPARATOR, IDM_NONE, NULL);
			trayEntriesMutex.Lock();
			for (const TrayEntry* it : trayEntries) InsertMenuA(hPopMenu, 0xFFFFFFFF, MF_STRING | MF_GRAYED, IDM_NONE, it->str.c_str());
			trayEntriesMutex.Unlock();
			InsertMenuA(hPopMenu, 0xFFFFFFFF, MF_SEPARATOR, IDM_NONE, NULL);
			InsertMenuA(hPopMenu, 0xFFFFFFFF, MF_STRING, IDM_EXIT, "Exit");
			SetForegroundWindow(hWnd); //cause the popup to be focused
			TrackPopupMenu(hPopMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, lpClickPoint.x, lpClickPoint.y, 0, hWnd, NULL);
		}

		static LRESULT CALLBACK Proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			static UINT s_WM_TASKBARRESTART;
			if (Msg == WM_COMMAND && wParam == IDM_EXIT) { NOTIFYICONDATAA i; ZeroMemory(&i, sizeof(i)); i.cbSize = sizeof(i); i.hWnd = hWnd; Shell_NotifyIconA(NIM_DELETE, &i); ExitProcess(EXIT_SUCCESS); }
			if (Msg == WM_USER && (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_RBUTTONUP)) OpenMenu(hWnd, true); //systray rightclick
			if (Msg == WM_CREATE || Msg == s_WM_TASKBARRESTART)
			{
				if (Msg == WM_CREATE) s_WM_TASKBARRESTART = RegisterWindowMessageA("TaskbarCreated");
				NOTIFYICONDATAA nid;
				ZeroMemory(&nid, sizeof(nid));
				nid.cbSize = sizeof(nid); 
				nid.hWnd = hWnd;
				nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; 
				nid.hIcon = LoadIconA(GetModuleHandleA(NULL), "ICN");
				nid.uCallbackMessage = WM_USER; 
				strcpy(nid.szTip, "MCLANRelay");
				Shell_NotifyIconA(NIM_ADD, &nid);
			}
			return DefWindowProcA(hWnd, Msg, wParam, lParam);
		}

		static DWORD WINAPI MessageThread(LPVOID)
		{
			WNDCLASSA c;
			ZeroMemory(&c, sizeof(c));
			c.lpfnWndProc = Wnd::Proc;
			c.hInstance = GetModuleHandleA(NULL);
			c.lpszClassName = "MCLANRelay";
			systrayHwnd = (RegisterClassA(&c) ? CreateWindowA(c.lpszClassName, 0, 0, 0, 0, 0, 0, 0, 0, c.hInstance, 0) : 0);
			if (!systrayHwnd) { LogMsgBox("Systray window error"); ExitProcess(EXIT_FAILURE); }
			MSG Msg;
			while (GetMessageA(&Msg, NULL, 0, 0) > 0) { TranslateMessage(&Msg); DispatchMessageA(&Msg); }
			return 0;
		}

		static void ExitSystray()
		{
			SendMessageA(systrayHwnd, WM_COMMAND, IDM_EXIT, 0);
			Sleep(10000);
		}
	};
	ExitSystray = Wnd::ExitSystray;
	CreateThread(NULL, 0, Wnd::MessageThread, NULL, 0, NULL);
}

#ifdef _DEBUG
#define LogText(fmt, ...) fprintf(stdout, fmt "\n", ## __VA_ARGS__)
#else
#pragma comment(linker, "/subsystem:windows")
#define LogText(fmt, ...) (void)0
#endif
#define LogError(fmt, ...) LogMsgBox(fmt, ## __VA_ARGS__)
#define ScopeTrayEntry(fmt, ...) TrayEntry _te(fmt, ## __VA_ARGS__)
#else //MCLANRELAY_USE_WIN32SYSTRAY
#define LogText(fmt, ...) fprintf(stdout, fmt "\n", ## __VA_ARGS__)
#define LogError(fmt, ...) fprintf(stderr, fmt "\n\n", ## __VA_ARGS__)
#define ScopeTrayEntry(fmt, ...)
#endif

static const char *bindAddress = NULL;

struct RelayConnection
{
	RelayConnection(sts_net_socket_t client, const char* serverHost, int serverPort) : aborted(false), client(client), serverHost(serverHost), serverPort(serverPort), thread((Thread::FUNC_t)ThreadFunc, this) { }

	bool aborted;

	private:
	sts_net_socket_t client;
	const char* serverHost;
	int serverPort;
	Thread thread;

	static Thread::RET_t THREAD_CC ThreadFunc(RelayConnection* conn)
	{
		char clientHost[16] = { 0 };
		sts_net_gethostname(&conn->client, clientHost, sizeof(clientHost), 1, NULL, 0);
		LogText("[RELAY CONNECTION] Setting up relay from client %s to server %s:%d", clientHost, conn->serverHost, conn->serverPort);
		ScopeTrayEntry("Relaying %s to %s:%d", clientHost, conn->serverHost, conn->serverPort);

		sts_net_socket_t server;
		if (sts_net_connect(&server, conn->serverHost, conn->serverPort))
		{
			LogText("[RELAY CONNECTION] Could not connect to server %s:%d, unable to setup relay", conn->serverHost, conn->serverPort);
		}
		else
		{
			sts_net_set_t set;
			sts_net_init_socket_set(&set);
			set.sockets[0] = conn->client;
			set.sockets[1] = server;

			const char* endReason = "Relay shut down";
			while (!conn->aborted)
			{
				char buf[4096];
				int ready = sts_net_check_socket_set(&set, 1.0f);
				if (ready < 0) { endReason = "Unknown network error"; break; }
				if (set.sockets[0].ready)
				{
					int got = sts_net_recv(&set.sockets[0], buf, sizeof(buf));
					if (got <= 0) { endReason = "Client disconnected"; break; }
					if (sts_net_send(&set.sockets[1], buf, got)) { endReason = "Lost connection to server"; break; }
				}
				if (set.sockets[1].ready)
				{
					int got = sts_net_recv(&set.sockets[1], buf, sizeof(buf));
					if (got <= 0) { endReason = "Lost connection to server"; break; }
					if (sts_net_send(&set.sockets[0], buf, got)) { endReason = "Client disconnected"; break; }
				}
			}
			LogText("[RELAY CONNECTION] Finished connection %s to %s:%d: %s", clientHost, conn->serverHost, conn->serverPort, endReason);
			sts_net_close_socket(&server);
		}
		sts_net_close_socket(&conn->client);
		conn->thread.Detach();
		delete conn;
		return 0;
	}
};

static int RunRelay(int relayPort, const char* serverHost, int serverPort, bool* broadcastActive = NULL, const char* multicastMotd = NULL)
{
	sts_net_socket_t relayServer;
	if (sts_net_listen(&relayServer, relayPort, bindAddress, 0) < 0)
	{
		LogError("[%s] Could not listen on %s%s%d - Aborting", (broadcastActive ? "SERVER" : "CLIENT"), (bindAddress ? bindAddress : "port"), (bindAddress ? ":" : " "), relayPort);
		return 1;
	}

	sts_net_socket_t multicast;
	char announce[128];
	int announcelen;
	if (!broadcastActive)
	{
		sts_net_udp_open(&multicast);
		announcelen = sprintf(announce, "%s%s%s[AD]%d[/AD]", "[MOTD]", multicastMotd, "[/MOTD]", (int)relayPort);
	}

	std::vector<RelayConnection*> connections;

	for (int missedBroadcasts = 0;;)
	{
		sts_net_socket_t client;
		int ready = sts_net_check_socket(&relayServer, 1.5);
		if (ready < 0) { LogError("[RELAY%d] Unknown error while accepting new connection", relayPort); break; }
		if (broadcastActive)
		{
			if (!ready && !*broadcastActive)
			{
				if (missedBroadcasts++ > 5)
				{
					LogText("[RELAY%d] Server went offline, relay not needed anymore, shutting down", relayPort);
					break;
				}
			}
			else
			{
				*broadcastActive = false;
				missedBroadcasts = 0;
			}
		}
		else
		{
			if (sts_net_udp_send(&multicast, "224.0.2.60", 4445, announce, announcelen))
			{
				LogError("[CLIENT] Unknown error while announce relay server on UDP multicast 224.0.2.60:4445, shutting down");
				break;
			}
			sleep_ms(1500);
		}
		if (ready)
		{
			if (sts_net_accept_socket(&relayServer, &client))
			{
				LogError("[RELAY%d] Unknown error while accepting new connection", relayPort);
				continue;
			}
			connections.push_back(new RelayConnection(client, serverHost, serverPort));
		}
	}

	for (RelayConnection* conn : connections) conn->aborted = true;
	sts_net_close_socket(&relayServer);
	return 0;
}

static std::vector<struct ServerRelay*> serverRelays;
static Mutex serverRelaysMutex;

struct ServerRelay
{
	static bool CreateIfNew(const char* serverHost, int serverPort, int firstRelayPort, const char* announcement)
	{
		int existingMinPort = 0x7FFFFFFF, existingMaxPort = 0;
		serverRelaysMutex.Lock();
		for (ServerRelay* relay : serverRelays)
		{
			if (relay->serverPort == serverPort && relay->serverHost == serverHost)
			{
				relay->broadcastActive = true;
				serverRelaysMutex.Unlock();
				return false;
			}
			if (relay->relayPort > existingMaxPort) existingMaxPort = relay->relayPort;
			if (relay->relayPort < existingMinPort) existingMinPort = relay->relayPort;
		}
		int relayPort = (existingMinPort <= firstRelayPort ? existingMaxPort + 1 : firstRelayPort);
		serverRelays.push_back(new ServerRelay(relayPort, serverHost, serverPort, announcement));
		serverRelaysMutex.Unlock();
		return true;
	}

	ServerRelay(int relayPort, const char* serverHost, int serverPort, const char* announcement) : broadcastActive(true), relayPort(relayPort), serverPort(serverPort), serverHost(serverHost)
	{
		const char *pMotd = strstr(announcement, "[MOTD]"), *pMotdEnd = strstr(announcement, "[/MOTD]");
		motd = (pMotd && pMotdEnd >= pMotd+6 ? std::string(pMotd + 6, pMotdEnd - pMotd - 6) : std::string(announcement));
		thread.Start((Thread::FUNC_t)ThreadFunc, this);
	}

	bool broadcastActive;

	private:
	int relayPort, serverPort;
	std::string serverHost, motd;
	Thread thread;

	static Thread::RET_t THREAD_CC ThreadFunc(ServerRelay* relay)
	{
		LogText("[SERVER] Set up new relay on port %d to server %s:%d '%s'", relay->relayPort, relay->serverHost.c_str(), relay->serverPort, relay->motd.c_str());
		ScopeTrayEntry("Listen port %d relay to server %s:%d '%s'", relay->relayPort, relay->serverHost.c_str(), relay->serverPort, relay->motd.c_str());
		RunRelay(relay->relayPort, relay->serverHost.c_str(), relay->serverPort, &relay->broadcastActive);
		Unregister(relay);
		relay->thread.Detach();
		delete relay;
		return 0;
	}

	static void Unregister(ServerRelay* relay)
	{
		serverRelaysMutex.Lock();
		size_t i;for (i = 0; i != serverRelays.size(); i++) if (serverRelays[i] == relay) break;
		serverRelays.erase(serverRelays.begin() + i);
		serverRelaysMutex.Unlock();
	}
};

struct ServerModeConfig { int firstRelayPort; };

static int THREAD_CC ServerWaitForMulticasts(const ServerModeConfig* cfg)
{
	sts_net_socket_t multicast;
	if (sts_net_udp_open(&multicast) || sts_net_udp_join_multicast(&multicast, "224.0.2.60") || sts_net_udp_bind(&multicast, 4445, bindAddress, 1))
	{
		LogError("[SERVER] Unable to listen to LAN server announcements on UDP multicast 224.0.2.60:4445 - aborting");
		return 1;
	}

	LogText("[SERVER] Waiting for LAN server announcements, first server will be relayed to port %d ...", cfg->firstRelayPort);
	ScopeTrayEntry("Waiting for LAN servers, relay to port %d", cfg->firstRelayPort);
	for (;;)
	{
		char buf[256];
		unsigned char ip[4];
		int got = sts_net_udp_recv_from(&multicast, buf, sizeof(buf), ip, NULL);
		if (got < 0)
		{
			LogError("[SERVER] Unable to listen to LAN server announcements on UDP multicast 224.0.2.60:4445 - aborting");
			return 1;
		}
		buf[got == sizeof(buf) ? sizeof(buf)-1 : got] = '\0';
		const char *bufAd = strstr(buf, "[AD]");
		int serverPort;
		if (!bufAd  || bufAd - buf + 4 >= got || !(serverPort = atoi(bufAd + 4)))
		{
			LogText("[SERVER] Received invalid server announcement from %d.%d.%d.%d (data: %.*s)", ip[0], ip[1], ip[2], ip[3], got, buf);
			continue;
		}
		if (!strstr(buf, " - ")) //" - " is "username - worldname" separator of real servers
		{
			// Ignore relays that are broadcasting in the same LAN
			continue;
		}

		char host[16];
		sprintf(host, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		ServerRelay::CreateIfNew(host, serverPort, cfg->firstRelayPort, buf);
	}
}

struct ClientModeConfig { const char* serverHost; int serverPort; int relayPort; const char* motd; };

static int THREAD_CC ClientWaitForConnection(const ClientModeConfig* cfg)
{
	LogText("[CLIENT] Announcing relay server '%s' on port %d to host %s:%d", cfg->motd, cfg->relayPort, cfg->serverHost, cfg->serverPort);
	ScopeTrayEntry("Relay server '%s' on port %d to host %s:%d", cfg->motd, cfg->relayPort, cfg->serverHost, cfg->serverPort);
	return RunRelay(cfg->relayPort, cfg->serverHost, cfg->serverPort, NULL, cfg->motd);
}

static void ShowHelp(const char* err = "")
{
	LogError("MCLANRelay " MCLANRELAY_VERSION " - Command Line Arguments" "\n\n" "%s"
		"-s [<port>]" "\n"
		"    Operate in server-side mode" "\n"
		"    All LAN servers on this network are relayed to consecutive" "\n"
		"    ports starting at <port> (default 25565)." "\n"
		"\n"
		"-c <online-server-host> [<server-port>] [<local-port>] [<name>]" "\n"
		"    Operate in client-side mode" "\n"
		"    A relay LAN server is announced on this network which gets relayed" "\n"
		"    to the online server at the given address." "\n"
		"    Without the second parameter, the default port 25565 will be used." "\n"
		"    If a number is given as a third parameter it will be used as the" "\n"
		"    port of the local relay server (default 51235)." "\n"
		"    In an optional last parameter the name that shows up in the server" "\n"
		"    browser can be customized (default 'MCLANRelay')." "\n"
		"\n"
		"-b <ip-address>  Specify the interface to listen on (defaults to all)" "\n"
		"\n"
		"-h               Show this help",
		err);
}

int main(int argc, char *argv[])
{
	bool serverMode = false, clientMode = false;
	ServerModeConfig serverCfg = { 25565 };
	ClientModeConfig clientCfg = { NULL, 25565, 51235, "MCLANRelay" };
	for (int i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-' && argv[i][0] != '/') continue;
		if (argv[i][1] == 'h' || argv[i][1] == 'H' || argv[i][1] == '?') { ShowHelp(); return 0; }
		if (argv[i][1] == 'b' && i < argc - 1) bindAddress = argv[++i];
		if (argv[i][1] == 's' || argv[i][1] == 'S')
		{
			if (i < argc - 1 && atoi(argv[i+1])) serverCfg.firstRelayPort = atoi(argv[++i]);
			serverMode = true;
		}
		if (argv[i][1] == 'c' || argv[i][1] == 'C')
		{
			if (i >= argc - 1 || argv[i+1][0] == '-' || argv[i+1][0] == '/') { ShowHelp("Error: Invalid arguments to -c\n\n"); return 1; }
			clientCfg.serverHost = argv[++i];
			if (i < argc - 1 && atoi(argv[i+1])) clientCfg.serverPort = atoi(argv[++i]);
			if (i < argc - 1 && atoi(argv[i+1])) clientCfg.relayPort = atoi(argv[++i]);
			if (i < argc - 1 && argv[i+1][0] != '-' && argv[i+1][0] != '/') clientCfg.motd = argv[++i];
			clientMode = true;
		}
	}

	if (!serverMode && !clientMode)
	{
		ShowHelp("Error: No mode specified\n\n");
		return 1;
	}

	if (sts_net_init() < 0)
	{
		LogError("Unknown network error, unable to start");
		return 1;
	}

	#ifdef MCLANRELAY_USE_WIN32SYSTRAY
	SetupSystray(serverMode, clientMode);
	#endif

	int ret;
	if (serverMode)
	{
		if (clientMode) Thread((Thread::FUNC_t)ClientWaitForConnection, &clientCfg).Detach();
		ret = ServerWaitForMulticasts(&serverCfg);
	}
	else
	{
		ret = ClientWaitForConnection(&clientCfg);
	}

	#ifdef MCLANRELAY_USE_WIN32SYSTRAY
	ExitSystray();
	#endif

	return ret;
}

#ifdef MCLANRELAY_USE_WIN32SYSTRAY
int WINAPI WinMain(HINSTANCE, HINSTANCE,LPSTR, int)
{
	return main(__argc, __argv);
}
#endif
