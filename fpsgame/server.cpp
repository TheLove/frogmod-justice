#include "game.h"

namespace game
{
	void parseoptions(vector<const char *> &args)
	{
		loopv(args)
#ifndef STANDALONE
			if(!game::clientoption(args[i]))
#endif
			if(!server::serveroption(args[i]))
				conoutf(CON_ERROR, "unknown command-line option: %s", args[i]);
	}

	const char *savedconfig() {
		return "config.cfg";
	}

	const char *defaultconfig() {
		return "server-init.cfg";
	}

	void writeclientinfo(stream *f) {
		server::writepbans(f);
		server::writeblacklist(f);
	}
}

extern ENetAddress masteraddress;
extern char *serverip;

namespace server
{
	struct server_entity            // server side version of "entity" type
	{
		int type;
		int spawntime;
		char spawned;
	};

	static const int DEATHMILLIS = 300;

	struct clientinfo;

	struct gameevent
	{
		virtual ~gameevent() {}

		virtual bool flush(clientinfo *ci, int fmillis);
		virtual void process(clientinfo *ci) {}

		virtual bool keepable() const { return false; }
	};

	struct timedevent : gameevent
	{
		int millis;

		bool flush(clientinfo *ci, int fmillis);
	};

	struct hitinfo
	{
		int target;
		int lifesequence;
		int rays;
		float dist;
		vec dir;
	};

	struct shotevent : timedevent
	{
		int id, gun;
		vec from, to;
		vector<hitinfo> hits;

		void process(clientinfo *ci);
	};

	struct explodeevent : timedevent
	{
		int id, gun;
		vector<hitinfo> hits;

		bool keepable() const { return true; }

		void process(clientinfo *ci);
	};

	struct suicideevent : gameevent
	{
		void process(clientinfo *ci);
	};

	struct pickupevent : gameevent
	{
		int ent;

		void process(clientinfo *ci);
	};

	template <int N>
	struct projectilestate
	{
		int projs[N];
		int numprojs;

		projectilestate() : numprojs(0) {}

		void reset() { numprojs = 0; }

		void add(int val)
		{
			if(numprojs>=N) numprojs = 0;
			projs[numprojs++] = val;
		}

		bool remove(int val)
		{
			loopi(numprojs) if(projs[i]==val)
			{
				projs[i] = projs[--numprojs];
				return true;
			}
			return false;
		}
	};

	struct gamestate : fpsstate
	{
		vec o;
		int state, editstate;
		int lastdeath, lastspawn, lifesequence;
		int lastshot;
		projectilestate<8> rockets, grenades;
		int frags, flags, deaths, teamkills, shotdamage, damage;
		int lasttimeplayed, timeplayed;
		float effectiveness;

		gamestate() : state(CS_DEAD), editstate(CS_DEAD), lifesequence(0) {}

		bool isalive(int gamemillis)
		{
			return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
		}

		bool waitexpired(int gamemillis)
		{
			return gamemillis - lastshot >= gunwait;
		}

		void reset()
		{
			if(state!=CS_SPECTATOR) state = editstate = CS_DEAD;
			maxhealth = 100;
			rockets.reset();
			grenades.reset();

			timeplayed = 0;
			effectiveness = 0;
			frags = flags = deaths = teamkills = shotdamage = damage = 0;

			respawn();
		}

		void respawn()
		{
			fpsstate::respawn();
			o = vec(-1e10f, -1e10f, -1e10f);
			lastdeath = 0;
			lastspawn = -1;
			lastshot = 0;
		}

		void reassign()
		{
			respawn();
			rockets.reset();
			grenades.reset();
		}
	};

	struct savedscore
	{
		uint ip;
		string name;
		int maxhealth, frags, flags, deaths, teamkills, shotdamage, damage;
		int timeplayed;
		float effectiveness;

		void save(gamestate &gs)
		{
			maxhealth = gs.maxhealth;
			frags = gs.frags;
			flags = gs.flags;
			deaths = gs.deaths;
			teamkills = gs.teamkills;
			shotdamage = gs.shotdamage;
			damage = gs.damage;
			timeplayed = gs.timeplayed;
			effectiveness = gs.effectiveness;
		}

		void restore(gamestate &gs)
		{
			if(gs.health==gs.maxhealth) gs.health = maxhealth;
			gs.maxhealth = maxhealth;
			gs.frags = frags;
			gs.flags = flags;
			gs.deaths = deaths;
			gs.teamkills = teamkills;
			gs.shotdamage = shotdamage;
			gs.damage = damage;
			gs.timeplayed = timeplayed;
			gs.effectiveness = effectiveness;
		}
	};

	extern int gamemillis, nextexceeded;

	VAR(kickmillis, 0, 5000, 65535); // number of milliseconds between two consecutive kicks
	VAR(maxkicks, 0, 2, 128); // number of kicks within kickmillis interval that will trigger mass kick protection
	struct clientinfo
	{
		int clientnum, ownernum, connectmillis, sessionid, overflow;
		string name, team, mapvote;
		int playermodel;
		int modevote;
		int privilege;
		bool connected, local, timesync, logged_in;
		int gameoffset, lastevent, pushed, exceeded;
		gamestate state;
		vector<gameevent *> events;
		vector<uchar> position, messages;
		int posoff, poslen, msgoff, msglen;
		vector<clientinfo *> bots;
		uint authreq;
		string authname;
		int ping, aireinit;
		string clientmap;
		int mapcrc;
		bool warned, gameclip;
		ENetPacket *clipboard;
		int lastclipboard, needclipboard;
		// mass kick protection:
		int lastkickmillis, nkicks; // last kick timestamp, number of kicks attempts since then
		bool warned_blacklisted; // avoid double "player is blacklisted" warnings...

		clientinfo() : clipboard(NULL) { reset(); }
		~clientinfo() { events.deletecontents(); cleanclipboard(); }

		void addevent(gameevent *e)
		{
			if(state.state==CS_SPECTATOR || events.length()>100) delete e;
			else events.add(e);
		}

		enum
		{
			PUSHMILLIS = 2500
		};

		int calcpushrange()
		{
			ENetPeer *peer = getclientpeer(ownernum);
			return PUSHMILLIS + (peer ? peer->roundTripTime + peer->roundTripTimeVariance : ENET_PEER_DEFAULT_ROUND_TRIP_TIME);
		}

		bool checkpushed(int millis, int range)
		{
			return millis >= pushed - range && millis <= pushed + range;
		}

		void scheduleexceeded()
		{
			if(state.state!=CS_ALIVE || !exceeded) return;
			int range = calcpushrange();
			if(!nextexceeded || exceeded + range < nextexceeded) nextexceeded = exceeded + range;
		}

		void setexceeded()
		{
			if(state.state==CS_ALIVE && !exceeded && !checkpushed(gamemillis, calcpushrange())) exceeded = gamemillis;
			scheduleexceeded(); 
		}
	
		void setpushed()
		{
			pushed = max(pushed, gamemillis);
			if(exceeded && checkpushed(exceeded, calcpushrange())) exceeded = 0;
		}

		bool checkexceeded()
		{
			return state.state==CS_ALIVE && exceeded && gamemillis > exceeded + calcpushrange();
		}

		void mapchange()
		{
			mapvote[0] = 0;
			state.reset();
			events.deletecontents();
			overflow = 0;
			timesync = false;
			lastevent = 0;
			exceeded = 0;
			pushed = 0;
			authname[0] = 0;
			clientmap[0] = 0;
			mapcrc = 0;
			warned = false;
			gameclip = false;
			lastkickmillis = 0;
			nkicks = 0;
		}

		void reassign()
		{
			state.reassign();
			events.deletecontents();
			timesync = false;
			lastevent = 0;
		}

		void cleanclipboard(bool fullclean = true)
		{
			if(clipboard) { if(--clipboard->referenceCount <= 0) enet_packet_destroy(clipboard); clipboard = NULL; }
			if(fullclean) lastclipboard = 0;
		}

		void reset()
		{
			name[0] = team[0] = 0;
			playermodel = -1;
			privilege = PRIV_NONE;
			connected = local = false;
			authreq = 0;
			position.setsize(0);
			messages.setsize(0);
			ping = 0;
			aireinit = 0;
			needclipboard = 0;
			logged_in = false;
			cleanclipboard();
			mapchange();
			warned_blacklisted = false;
		}

		int geteventmillis(int servmillis, int clientmillis)
		{
			if(!timesync || (events.empty() && state.waitexpired(servmillis)))
			{
				timesync = true;
				gameoffset = servmillis - clientmillis;
				return servmillis;
			}
			else return gameoffset + clientmillis;
		}
	};

	struct worldstate
	{
		int uses;
		vector<uchar> positions, messages;
	};

	struct ban
	{
		int time;
		string pattern;
		string name;
	};
	
	struct black_ip {
		string pattern;
		string reason;
	};

	namespace aiman
	{
		extern void removeai(clientinfo *ci);
		extern void clearai();
		extern void checkai();
		extern void reqadd(clientinfo *ci, int skill);
		extern void reqdel(clientinfo *ci);
		extern void setbotlimit(clientinfo *ci, int limit);
		extern void setbotbalance(clientinfo *ci, bool balance);
		extern void changemap();
		extern void addclient(clientinfo *ci);
		extern void changeteam(clientinfo *ci);
	}

	#define MM_MODE 0xF
	#define MM_AUTOAPPROVE 0x1000
	#define MM_PRIVSERV (MM_MODE | MM_AUTOAPPROVE)
	#define MM_PUBSERV ((1<<MM_OPEN) | (1<<MM_VETO))
	#define MM_COOPSERV (MM_AUTOAPPROVE | MM_PUBSERV | (1<<MM_LOCKED))

	bool notgotitems = true;        // true when map has changed and waiting for clients to send item
	int gamemode = 0;
	int gamemillis = 0, gamelimit = 0, nextexceeded = 0;
	bool gamepaused = false;

	string smapname = "";
	int interm = 0;
	bool mapreload = false;
	enet_uint32 lastsend = 0;
	int mastermode = MM_OPEN, mastermask = MM_PRIVSERV;
	int currentmaster = -1;
	stream *mapdata = NULL;

	vector<uint> allowedips;
	vector<ban> bannedips;
	vector<black_ip> blacklistips;
	vector<clientinfo *> connects, clients, bots;
	vector<worldstate *> worldstates;
	bool reliablemessages = false;

	struct demofile
	{
		string info;
		uchar *data;
		int len;
	};

	#define MAXDEMOS 5
	vector<demofile> demos;

	bool demonextmatch = false;
	stream *demotmp = NULL, *demorecord = NULL, *demoplayback = NULL;
	int nextplayback = 0, demomillis = 0;

	SVAR(serverdesc, "");
	SVAR(serverpass, "");
	SVAR(adminpass, "");
	VARF(publicserver, 0, 0, 2, {
		switch(publicserver)
		{
			case 0: default: mastermask = MM_PRIVSERV; break;
			case 1: mastermask = MM_PUBSERV; break;
			case 2: mastermask = MM_COOPSERV; break;
		}
	});
	SVAR(servermotd, "");
	SVAR(mastermessage, "");

	void *newclientinfo() { return new clientinfo; }
	void deleteclientinfo(void *ci) { delete (clientinfo *)ci; }

	clientinfo *getinfo(int n)
	{
		if(n < MAXCLIENTS) return (clientinfo *)getclientinfo(n);
		n -= MAXCLIENTS;
		return bots.inrange(n) ? bots[n] : NULL;
	}

	vector<server_entity> sents;
	vector<savedscore> scores;

	int msgsizelookup(int msg)
	{
		static int sizetable[NUMSV] = { -1 };
		if(sizetable[0] < 0)
		{
			memset(sizetable, -1, sizeof(sizetable));
			for(const int *p = msgsizes; *p >= 0; p += 2) sizetable[p[0]] = p[1];
		}
		return msg >= 0 && msg < NUMSV ? sizetable[msg] : -1;
	}

	const char *modename(int n, const char *unknown)
	{
		if(m_valid(n)) return gamemodes[n - STARTGAMEMODE].name;
		return unknown;
	}

	const char *mastermodename(int n, const char *unknown)
	{
		return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodenames)/sizeof(mastermodenames[0])) ? mastermodenames[n-MM_START] : unknown;
	}

	const char *privname(int type)
	{
		switch(type)
		{
			case PRIV_ADMIN: return "admin";
			case PRIV_MASTER: return "master";
			default: return "unknown";
		}
	}

	void sendservmsg(const char *s) { sendf(-1, 1, "ris", N_SERVMSG, s); }

	void resetitems()
	{
		sents.shrink(0);
		//cps.reset();
	}

	bool serveroption(const char *arg)
	{
		if(arg[0]=='-') switch(arg[1])
		{
			case 'n': setsvar("serverdesc", &arg[2]); return true;
			case 'y': setsvar("serverpass", &arg[2]); return true;
			case 'p': setsvar("adminpass", &arg[2]); return true;
			case 'o': setvar("publicserver", atoi(&arg[2])); return true;
			case 'g': setvar("serverbotlimit", atoi(&arg[2])); return true;
		}
		return false;
	}

	/*****************************
	 *  HTTP Server
	 *****************************/

	VAR(httpport, 0, 0, 65535);
	evhttp *http;

	static const char *find_mime_type(const char *filename) {
		const char *ext = strrchr(filename, '.');
		if(ext) {
			if(!strcasecmp(ext, ".css")) return "text/css";
			if(!strcasecmp(ext, ".js")) return "text/javascript";
			if(!strcasecmp(ext, ".html")) return "text/html";
			if(!strcasecmp(ext, ".jpg")) return "image/jpeg";
			if(!strcasecmp(ext, ".jpeg")) return "image/jpeg";
			if(!strcasecmp(ext, ".png")) return "image/png";
			if(!strcasecmp(ext, ".gif")) return "image/gif";
			if(!strcasecmp(ext, ".ico")) return "image/x-icon";
			if(!strcasecmp(ext, ".json")) return "application/json";
		}
		return "text";
	}

	void evhttp_request_add_content_type(evhttp_request *req, const char *filename) {
		evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", find_mime_type(filename));
	}

	void evhttp_serve_file(evhttp_request *req, const char *filename) {
		char *q = strchr((char *)filename, '?'); if(q) *q = 0; // remove ?foo=bar params
		char *s = strrchr((char *)filename, '/'); // basename
		if(s) filename = s+1;
		int len;
		char *contents = NULL;
		defformatstring(path)("web/%s", filename);
		if(*filename) contents = loadfile(path, &len);
		if(contents) {
			evhttp_request_add_content_type(req, filename);
			evhttp_add_header(evhttp_request_get_output_headers(req), "Cache-Control", "max-age=36000"); // 10hrs
			evbuffer *buf = evbuffer_new();
			evbuffer_add(buf, contents, len);
			evhttp_send_reply(req, 200, "OK", buf);
			evbuffer_free(buf);
			delete[] contents;
		} else evhttp_send_error(req, 404, "Not Found");
	}

	static void httpgencb(evhttp_request *req, void *arg) {
		evhttp_serve_file(req, evhttp_request_get_uri(req));
	}

	void evbuffer_add_json_string(evbuffer *buf, const char *str) {
		if(!str) return;
		const char *c = str;
		while(*c) {
			switch(*c) {
				case '\n': evbuffer_add_printf(buf, "\\n"); break;
				case '\f': evbuffer_add_printf(buf, "\\f"); break;
				case '\r': evbuffer_add_printf(buf, "\\r"); break;
				case '\t': evbuffer_add_printf(buf, "\\t"); break;
				case '/': evbuffer_add_printf(buf, "\\/"); break;
				case '"':  evbuffer_add_printf(buf, "\\\""); break;
				case '\\': evbuffer_add_printf(buf, "\\\\"); break;
				default: evbuffer_add_printf(buf, "%c", *c); break;
			}
			c++;
		}
	}

	void evbuffer_add_json_prop(evbuffer *buf, const char *name, const char *val, bool comma=true) {
		evbuffer_add_printf(buf, "\t\"");
		evbuffer_add_json_string(buf, name);
		evbuffer_add_printf(buf, "\": \"");
		evbuffer_add_json_string(buf, val);
		evbuffer_add_printf(buf, "\"%s\n", comma?",":"");
	}

	void evbuffer_add_json_prop(evbuffer *buf, const char *name, int val, bool comma=true) {
		evbuffer_add_printf(buf, "\t\"");
		evbuffer_add_json_string(buf, name);
		evbuffer_add_printf(buf, "\": %d%s\n", val, comma?",":"");
	}

	void evbuffer_add_json_prop(evbuffer *buf, const char *name, ev_uint64_t val, bool comma=true) {
		evbuffer_add_printf(buf, "\t\"");
		evbuffer_add_json_string(buf, name);
		evbuffer_add_printf(buf, "\": %llu%s\n", val, comma?",":"");
	}

	void evbuffer_add_json_prop(evbuffer *buf, const char *name, float val, bool comma=true) {
		evbuffer_add_printf(buf, "\t\"");
		evbuffer_add_json_string(buf, name);
		evbuffer_add_printf(buf, "\": %f%s\n", val, comma?",":"");
	}


	void evbuffer_add_json_server(evbuffer *buf, bool comma = true) {
		evbuffer_add_printf(buf, "\"server\": ");
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_prop(buf, "name", serverdesc);
		evbuffer_add_json_prop(buf, "port", getvar("serverport"));
		evbuffer_add_json_prop(buf, "time", totalmillis);
		evbuffer_add_json_prop(buf, "map", smapname);
		evbuffer_add_json_prop(buf, "gamemode", modename(gamemode), false);
		evbuffer_add_printf(buf, "}%s", comma ? "," : "");
	}

	void evbuffer_add_json_player_simple(evbuffer *buf, const char *name, clientinfo *ci, bool comma = true) {
		evbuffer_add_printf(buf, "\"");
		evbuffer_add_json_string(buf, name);
		evbuffer_add_printf(buf, "\": ");
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_prop(buf, "name", ci->name);
		evbuffer_add_json_prop(buf, "ip", getclientipstr(ci->clientnum));
		evbuffer_add_json_prop(buf, "hostname", getclienthostname(ci->clientnum));
		evbuffer_add_json_prop(buf, "skill", ci->state.skill, false);
		evbuffer_add_printf(buf, "}%s", comma ? "," : "");
	}


	void evbuffer_add_json_player(evbuffer *buf, clientinfo *ci, bool country, bool comma = true) {
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_prop(buf, "name", ci->name);
		evbuffer_add_json_prop(buf, "ip", getclientipstr(ci->clientnum));
		evbuffer_add_json_prop(buf, "hostname", getclienthostname(ci->clientnum));
#ifdef HAVE_GEOIP
		if(country) evbuffer_add_json_prop(buf, "country", getclientcountrynul(ci->clientnum));
#endif
		evbuffer_add_json_prop(buf, "skill", ci->state.skill);
		evbuffer_add_json_prop(buf, "team", ci->team);
		evbuffer_add_json_prop(buf, "clientnum", ci->clientnum);
		evbuffer_add_json_prop(buf, "privilege", ci->privilege);
		evbuffer_add_json_prop(buf, "connectmillis", totalmillis - ci->connectmillis);
		evbuffer_add_json_prop(buf, "playermodel", ci->playermodel);
		evbuffer_add_json_prop(buf, "authname", ci->authname);
		evbuffer_add_json_prop(buf, "ping", ci->ping);
		evbuffer_add_printf(buf, "\"o\": [ %f, %f, %f ],", ci->state.o.x, ci->state.o.y, ci->state.o.z);
		evbuffer_add_json_prop(buf, "state", ci->state.state);
		evbuffer_add_json_prop(buf, "editstate", ci->state.editstate);
		evbuffer_add_json_prop(buf, "frags", ci->state.frags);
		evbuffer_add_json_prop(buf, "flags", ci->state.flags);
		evbuffer_add_json_prop(buf, "deaths", ci->state.deaths);
		evbuffer_add_json_prop(buf, "teamkills", ci->state.teamkills);
		evbuffer_add_json_prop(buf, "shotdamage", ci->state.shotdamage);
		evbuffer_add_json_prop(buf, "damage", ci->state.damage);
		evbuffer_add_json_prop(buf, "effectiveness", (float)ci->state.effectiveness, false);
		evbuffer_add_printf(buf, "}%s", comma ? "," : "");
	}

	void evhttp_request_redirect(evhttp_request *req, const char *url) {
		evhttp_add_header(evhttp_request_get_output_headers(req), "Location", url);
		evhttp_send_reply(req, 302, "Found", NULL);
	}

	static void httpinfocb(evhttp_request *req, void *arg) {
		evhttp_request_add_content_type(req, ".json");
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{\n");
		evbuffer_add_json_prop(buf, "serverdesc", serverdesc);
		evbuffer_add_json_prop(buf, "servermotd", servermotd);
		evbuffer_add_json_prop(buf, "mastermessage", mastermessage);
		evbuffer_add_json_prop(buf, "maxclients", maxclients, false);
		evbuffer_add_printf(buf, "}");
		evhttp_send_reply(req, 200, "OK", buf);
		evbuffer_free(buf);
	}

	static void httpstatuscb(struct evhttp_request *req, void *arg) {
		evhttp_request_add_content_type(req, ".json");
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{\n");
		evbuffer_add_json_prop(buf, "totalmillis", totalmillis);
		evbuffer_add_json_prop(buf, "mastermode", mastermode);
		evbuffer_add_json_prop(buf, "mastermodename", mastermodename(mastermode));
		evbuffer_add_json_prop(buf, "mastermask", mastermask);
		evbuffer_add_json_prop(buf, "gamemode", gamemode);
		evbuffer_add_json_prop(buf, "gamemodename", modename(gamemode));
		evbuffer_add_json_prop(buf, "gamemillis", gamemillis);
		evbuffer_add_json_prop(buf, "gamelimit", gamelimit);
		evbuffer_add_json_prop(buf, "map", smapname, false);
		evbuffer_add_printf(buf, "}");
		evhttp_send_reply(req, 200, "OK", buf);
		evbuffer_free(buf);
	}

	static void httpplayerscb(struct evhttp_request *req, void *arg) {
		evhttp_request_add_content_type(req, ".json");
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "[\n");
		loopv(clients) {
			clientinfo *ci = clients[i];
			if(!ci) continue;
			evbuffer_add_json_player(buf, ci, true, i < clients.length() - 1);
		}
		evbuffer_add_printf(buf, "]");
		evhttp_send_reply(req, 200, "OK", buf);
		evbuffer_free(buf);
	}

	bool checkhttpauth(struct evhttp_request *req) {
		struct evkeyvalq *k = evhttp_request_get_input_headers(req);
		const char *auth = evhttp_find_header(k, "Authorization");
		if(adminpass[0] && auth) {
			string auth_type, auth_string;
			sscanf(auth, "%20s %150s", auth_type, auth_string);
			defformatstring(pass)("admin:%s", adminpass);
			return base64_strcmp(pass, auth_string);
		}
		return false;
	}

	void spectator(int, int);
	void setmaster(clientinfo *ci, bool val, const char *pass = "", const char *authname = NULL, bool no_open = false);
	void kick_client(int cn, clientinfo *m = NULL);
	static void httpadmincb(struct evhttp_request *req, void *arg) {
		if(checkhttpauth(req)) {
			evkeyvalq query;
			evhttp_parse_query_str(evhttp_uri_get_query(evhttp_request_get_evhttp_uri(req)), &query);
			const char *val;
			if((val = evhttp_find_header(&query, "command"))) {
				httpoutbuf = evbuffer_new();
				execute(val);
				evhttp_send_reply(req, 200, "OK", httpoutbuf);
				evbuffer_free(httpoutbuf);
				httpoutbuf = NULL;
			} else evhttp_serve_file(req, "admin.html");
		} else {
			struct evkeyvalq *o = evhttp_request_get_output_headers(req);
			evhttp_add_header(o, "WWW-Authenticate", "Basic realm=\"Secure Area\"");
			evbuffer *buf = evbuffer_new();
			evbuffer_add_printf(buf, "<h1>Authorization Required</h1>");
			evhttp_send_reply(req, 401, "Authorization Required", buf);
			evbuffer_free(buf);
		}
	}

	evbuffer *httpoutbuf;
	ev_uint64_t loglineid = 1;
	struct logline {
		struct timeval tv;
		ev_uint64_t id;
		char *line;
	};
	vector<logline> lastloglines;

	struct chatreq {
		evhttp_request *req;
	};
	vector<chatreq> reqs;
	void evbuffer_add_logline(evbuffer *buf, logline *l, bool comma=true) {
		evbuffer_add_printf(buf, "{ ");
		evbuffer_add_json_prop(buf, "ts", (ev_uint64_t)l->tv.tv_sec);
		evbuffer_add_json_prop(buf, "id", l->id);
		evbuffer_add_json_prop(buf, "line", l->line, false);
		evbuffer_add_printf(buf, "}%c", comma ? ',' : ' ');
	}

	static void httplogcb(struct evhttp_request *req, void *arg) {
		if(checkhttpauth(req)) {
			evkeyvalq query;
			evhttp_parse_query_str(evhttp_uri_get_query(evhttp_request_get_evhttp_uri(req)), &query);
			const char *val;
			ev_uint64_t last_id = 0;
			if((val = evhttp_find_header(&query, "last")) && *val) {
				last_id = atoi(val);
			}
			bool has = false;
			loopv(lastloglines) if(lastloglines[i].id > last_id) has = true;
			if(has) {
				evbuffer *buf = evbuffer_new();
				evbuffer_add_printf(buf, "[\n");
				loopv(lastloglines) if(lastloglines[i].id > last_id) evbuffer_add_logline(buf, &lastloglines[i], i < lastloglines.length() - 1);
				evbuffer_add_printf(buf, "]");
				evhttp_send_reply(req, 200, "OK", buf);
				evbuffer_free(buf);
			} else {
				evhttp_send_reply_start(req, 200, "OK");
				chatreq &r = reqs.add();
				r.req = req;
			}
		} else evhttp_send_error(req, 404, "Not Found");
	}

	VAR(memlogsize, 0, 20, 1000);
	void httplog(const char *line) {
		// memory log
		logline &l = lastloglines.add();
		gettimeofday(&l.tv, NULL);
		l.line = strdup(line);
		l.id = loglineid++;
		while(lastloglines.length() > memlogsize) {
			if(lastloglines[0].line) free(lastloglines[0].line);
			lastloglines.remove(0);
		}
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "[");
		evbuffer_add_logline(buf, &l, false);
		evbuffer_add_printf(buf, "]");
		loopv(reqs) {
			evhttp_send_reply_chunk(reqs[i].req, buf);
			evhttp_send_reply_end(reqs[i].req);
		}
		evbuffer_free(buf);
		reqs.shrink(0);
	}

	static void httpindexcb(struct evhttp_request *req, void *arg) {
		evhttp_serve_file(req, "index.html");
	}

	void httpinit() {
		http = evhttp_new(evbase);
		if(!httpport) return;
		if(evhttp_bind_socket(http, serverip[0]?serverip:NULL, httpport) < 0) {
			conoutf("Could not create http server on %s:%d\n", serverip, httpport);
			evhttp_free(http);
			return;
		}
		evhttp_set_cb(http, "/", httpindexcb, NULL);
		evhttp_set_cb(http, "/info", httpinfocb, NULL);
		evhttp_set_cb(http, "/status", httpstatuscb, NULL);
		evhttp_set_cb(http, "/players", httpplayerscb, NULL);
		evhttp_set_cb(http, "/admin", httpadmincb, NULL);
		evhttp_set_cb(http, "/log", httplogcb, NULL);
		evhttp_set_gencb(http, httpgencb, NULL);
	}


	/*****************************
	 *  HTTP Client
	 *****************************/

	SVAR(httphook, "");
	enum {
		HOOKFLAG_NOCONNECT = 1,
		HOOKFLAG_NODISCONNECT = 2,
		HOOKFLAG_NOKILL = 4,
		HOOKFLAG_NOKICK = 8,
		HOOKFLAG_NOSUICIDE = 16,
		HOOKFLAG_NOINTERMISSION = 32,
		HOOKFLAG_NONAMECHANGE = 64,
		HOOKFLAG_NOBLACKLIST = 128,
	};
	VAR(httphook_flags, 0, HOOKFLAG_NOKILL, 65535);

	static void http_event_cb(struct evhttp_request *req, void *arg) {
		if(!req) {
			printf("HTTP request failed.\n");
			return;
		}
		char *line;
		evbuffer *buf = evhttp_request_get_input_buffer(req);
		while((line = evbuffer_readln_nul(buf, NULL, EVBUFFER_EOL_ANY))) {
			printf("HTTP: %s\n", line);
			free(line);
		}
	}

	int http_hook_has_flag(int flag) {
		return httphook_flags & flag;
	}

	evhttp_connection *httpcon = NULL; // global variable - we reuse the connection
	void http_con_close_cb(evhttp_connection *con, void *arg) {
		httpcon = NULL; // just mark it as closed and reopen when needed
	}

	void http_post_evbuffer(evbuffer *buffer) {
		if(httphook[0]) {
			evhttp_uri *uri = evhttp_uri_parse(httphook);
			if(!uri) return;
			if(!httpcon) httpcon = evhttp_connection_base_new(evbase, dnsbase, evhttp_uri_get_host(uri), evhttp_uri_get_port(uri)==-1?80:evhttp_uri_get_port(uri));
			if(httpcon) {
				evhttp_connection_set_closecb(httpcon, http_con_close_cb, NULL);
				evhttp_connection_set_retries(httpcon, 2);
				if(serverip[0]) evhttp_connection_set_local_address(httpcon, serverip);
				evhttp_request *req = evhttp_request_new(http_event_cb, NULL);
				evbuffer_add_buffer(evhttp_request_get_output_buffer(req), buffer);
				evhttp_add_header(evhttp_request_get_output_headers(req), "Host", evhttp_uri_get_host(uri));
				evhttp_make_request(httpcon, req, EVHTTP_REQ_POST, evhttp_uri_get_path(uri));
			}
			evhttp_uri_free(uri);
		}
	}

	void http_post_event_connect(clientinfo *ci) {
		if(http_hook_has_flag(HOOKFLAG_NOCONNECT)) return;
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "connect");
		evbuffer_add_json_player_simple(buf, "player", ci, false);
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}

	void http_post_event_namechange(clientinfo *ci, char *newName) {
		if(http_hook_has_flag(HOOKFLAG_NONAMECHANGE)) return;
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "connect");
		evbuffer_add_json_player_simple(buf, "player", ci);
		evbuffer_add_json_prop(buf, "newName", newName);
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}

	void http_post_event_disconnect(clientinfo *ci) {
		if(http_hook_has_flag(HOOKFLAG_NODISCONNECT)) return;
		defformatstring(connectionTimeString)("%d", totalmillis - ci->connectmillis);
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "disconnect");
		evbuffer_add_json_player_simple(buf, "player", ci);
		evbuffer_add_json_prop(buf, "connectionTime", connectionTimeString, false);
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}

	void http_post_event_kick(clientinfo *player, clientinfo *target) {
		if(http_hook_has_flag(HOOKFLAG_NOKICK)) return;
		defformatstring(connectionTimeString)("%d", totalmillis - target->connectmillis);
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "kick");
		if(player)
			evbuffer_add_json_player_simple(buf, "player", player);
		evbuffer_add_json_player_simple(buf, "target", target);
		evbuffer_add_json_prop(buf, "connectionTime", connectionTimeString, false);
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}

	void http_post_event_kill(clientinfo *player, clientinfo *target, int gun) {
		if(http_hook_has_flag(HOOKFLAG_NOKILL)) return;
		defformatstring(gunString)("%d", gun);
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "kill");
		evbuffer_add_json_player_simple(buf, "player", player);
		evbuffer_add_json_player_simple(buf, "target", target);
		evbuffer_add_json_prop(buf, "gun", gunString, false);
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}

	void http_post_event_suicide(clientinfo *player) {
		if(http_hook_has_flag(HOOKFLAG_NOSUICIDE)) return;
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "suicide");
		evbuffer_add_json_player_simple(buf, "player", player, false);
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}

	void http_post_event_intermission()
	{
		if(http_hook_has_flag(HOOKFLAG_NOINTERMISSION)) return;
		evbuffer *buf = evbuffer_new();
		evbuffer_add_printf(buf, "{");
		evbuffer_add_json_server(buf);
		evbuffer_add_json_prop(buf, "type", "intermission");
		evbuffer_add_printf(buf, "\"players\": ");
		evbuffer_add_printf(buf, "[");
		loopv(clients) {
			clientinfo *ci = clients[i];
			if(!ci) continue;
			evbuffer_add_json_player(buf, ci, false, i < clients.length() - 1);
		}
		evbuffer_add_printf(buf, "]");
		evbuffer_add_printf(buf, "}");
		http_post_evbuffer(buf);
		evbuffer_free(buf);
	}


	void http_post_event(const char *first, ...) {
		va_list ap;
		evbuffer *evb = evbuffer_new();
		evbuffer_add_printf(evb, "{\n");
		evbuffer_add_json_prop(evb, "serverdesc", serverdesc);
		evbuffer_add_json_prop(evb, "serverport", getvar("serverport"));
		evbuffer_add_json_prop(evb, "totalmillis", totalmillis);
		const char *name = first, *value;
		va_start(ap, first);
		while(name) {
			value = va_arg(ap, const char *);
			const char *nname = va_arg(ap, const char *);
			evbuffer_add_json_prop(evb, name, value, nname!=NULL);
			name = nname;
		}
		evbuffer_add_printf(evb, "}");
		http_post_evbuffer(evb);
		evbuffer_free(evb);
	}

	const char *colorname(clientinfo *ci, char *name = NULL, bool forcecn = false);
	bool checkblacklist(clientinfo *ci, char **reason = NULL);
	void gothostname(void *info) {
		clientinfo *ci = (clientinfo *)info;
		loopv(bannedips) if(!fnmatch(bannedips[i].pattern, getclienthostname(ci->clientnum), 0)) { disconnect_client(ci->clientnum, DISC_IPBAN); return; }
		char *reason = (char *)"";
		if(checkblacklist(ci, &reason) && !ci->warned_blacklisted) {
			if(ci->name && ci->name[0] && reason && reason[0]) {
				outf(2, "\f3WARNING: Player \"\f6%s\f3\" is blacklisted: \"\f7%s\f3\"", colorname(ci, NULL, true), reason);
				ci->warned_blacklisted = true;
			}
		}
	}

	clientinfo *scriptclient;
	IRC::Source *scriptircsource = NULL;
	ICOMMAND(login, "s", (char *s), {
		if(s && *s && *adminpass && !strcmp(s, adminpass)) {
			if(scriptircsource) {
				scriptircsource->peer->data[0] = 1; // flag as logged in
				outf(2, "%s logged in", scriptircsource->peer->nick);
			}
			if(scriptclient) {
				scriptclient->logged_in = true;
				outf(2, "%s logged in", colorname(scriptclient));
			}
		}
	});

	SVAR(frogchar, "@");
	void processcommand(char *txt, int privilege = 0);
	void ircmsgcb(IRC::Source *source, char *msg) {
		if(NULL == strchr(frogchar, *msg)) {
			char *s = color_irc2sauer(msg);
			outf(1 | OUT_NOIRC, "\f4%s \f2<%s> \f7%s", source->channel?source->channel->alias:"", source->peer->nick, s);
			free(s);
		} else {
			scriptircsource = source;
			if(source && source->peer && source->peer->data[0]) execute(msg+1);
			else processcommand(msg+1);
			scriptircsource = NULL;
		}
	}

	void ircprivcb(IRC::Source *source, char *msg) {
		scriptircsource = source;
		if(source && source->peer && source->peer->data[0]) execute(msg);
		else processcommand(msg);
		scriptircsource = NULL;
	}

	void ircactioncb(IRC::Source *source, char *msg) {
		char *s = color_irc2sauer(msg);
		outf(1 | OUT_NOIRC, "\f4%s \f1* %s \f7%s", source->channel->alias, source->peer->nick, s);
		free(s);
	}
	void ircnoticecb(IRC::Server *s, char *prefix, char *trailing) {
		if(prefix) outf(2 | OUT_NOIRC, "\f2[%s]\f1 -%s- %s\f7", s->alias, prefix, trailing);
		else outf(2 | OUT_NOIRC, "\f2[%s]\f1 %s\f7", s->alias, trailing);
	}
	void ircpingcb(IRC::Server *s, char *prefix, char *trailing) {
		outf(2 | OUT_NOIRC | OUT_NOGAME, "\f2[%s PING/PONG]\f1 %s\f7", s->alias, trailing?trailing:"");
	}
	void ircjoincb(IRC::Source *s) {
		outf(2 | OUT_NOIRC, "\f4%s \f1%s\f7 has joined", s->channel->alias, s->peer->nick);
	}
	void ircpartcb(IRC::Source *s, char *reason) {
		if(reason) outf(2 | OUT_NOIRC, "\f4 %s \f1%s\f7 \f4has parted (%s)", s->channel->alias, s->peer->nick, reason);
		else outf(2 | OUT_NOIRC, "\f4%s \f1%s\f7 has parted", s->channel->alias, s->peer->nick);
	}
	void ircquitcb(IRC::Source *s, char *reason) {
		if(reason) outf(2 | OUT_NOIRC, "\f4%s \f1%s\f7 has left (%s)", s->server->alias, s->peer->nick, reason);
		else outf(2 | OUT_NOIRC, "\f4%s \f1%s\f7 has left", s->server->alias, s->peer->nick);
	}
	void ircmodecb(IRC::Source *s, char *who, char *mode, char *target) {
		outf(2 | OUT_NOIRC | OUT_NOGAME, "\f4%s \f1*%s\f7 sets mode %s \f1%s", s->channel?s->channel->alias:s->server->alias, s->peer?s->peer->nick:who, mode, target);
	}
	void ircnickcb(IRC::Source *s, char *newnick) {
		outf(2 | OUT_NOIRC, "\f4%s \f1%s \f7 is now known as \f1%s", s->server->alias, s->peer->nick, newnick);
	}
	void irctopiccb(IRC::Source *s, char *topic) {
		outf(2 | OUT_NOIRC, "\f4%s \f1%s\f7 sets topic: \f2%s", s->channel->alias, s->peer->nick, topic);
	}
	void ircversioncb(IRC::Source *s) {
		evbuffer *eb = evbuffer_new();
		evbuffer_add_printf(eb, "NOTICE %s :\001VERSION Frogmod irc client " FROGMOD_VERSION "\001\r\n", s->peer->nick);
		bufferevent_write_buffer(s->server->buf, eb);
		evbuffer_free(eb);
	}
	ICOMMAND(ircecho, "C", (const char *msg), {
		char *s = color_sauer2irc((char *)msg);
		if(scriptircsource) scriptircsource->speak(s);
		free(s);
	});

	void ircinit() {
		irc.channel_message_cb        = ircmsgcb;
		irc.private_message_cb        = ircprivcb;
		irc.channel_action_message_cb = ircactioncb;
		irc.private_action_message_cb = NULL;
		irc.notice_cb                 = ircnoticecb;
		irc.motd_cb                   = ircnoticecb;
		irc.ping_cb                   = ircpingcb;
		irc.join_cb                   = ircjoincb;
		irc.part_cb                   = ircpartcb;
		irc.quit_cb                   = ircquitcb;
		irc.mode_cb                   = ircmodecb;
		irc.nick_cb                   = ircnickcb;
		irc.topic_cb                  = irctopiccb;
		irc.version_cb                = ircversioncb;
	}

	void serverinit() {
		smapname[0] = '\0';
		resetitems();
		httpinit();
		ircinit();
	}

	int numclients(int exclude = -1, bool nospec = true, bool noai = true, bool priv = false)
	{
		int n = 0;
		loopv(clients) 
		{
			clientinfo *ci = clients[i];
			if(ci->clientnum!=exclude && (!nospec || ci->state.state!=CS_SPECTATOR || (priv && (ci->privilege || ci->local))) && (!noai || ci->state.aitype == AI_NONE)) n++;
		}
		return n;
	}

	bool duplicatename(clientinfo *ci, char *name)
	{
		if(!name) name = ci->name;
		loopv(clients) if(clients[i]!=ci && !strcmp(name, clients[i]->name)) return true;
		return false;
	}

	const char *colorname(clientinfo *ci, char *name, bool forcecn)
	{
		if(!name) name = ci->name;
		if(name[0] && !duplicatename(ci, name) && !forcecn && ci->state.aitype == AI_NONE) return name;
		static string cname[3];
		static int cidx = 0;
		cidx = (cidx+1)%3;
		formatstring(cname[cidx])(ci->state.aitype == AI_NONE ? "%s \fs\f5(%d)\fr" : "%s \fs\f5[%d]\fr", name, ci->clientnum);
		return cname[cidx];
	}

	struct servmode
	{
		virtual ~servmode() {}

		virtual void entergame(clientinfo *ci) {}
		virtual void leavegame(clientinfo *ci, bool disconnecting = false) {}

		virtual void moved(clientinfo *ci, const vec &oldpos, bool oldclip, const vec &newpos, bool newclip) {}
		virtual bool canspawn(clientinfo *ci, bool connecting = false) { return true; }
		virtual void spawned(clientinfo *ci) {}
		virtual int fragvalue(clientinfo *victim, clientinfo *actor)
		{
			if(victim==actor || isteam(victim->team, actor->team)) return -1;
			return 1;
		}
		virtual void died(clientinfo *victim, clientinfo *actor) {}
		virtual bool canchangeteam(clientinfo *ci, const char *oldteam, const char *newteam) { return true; }
		virtual void changeteam(clientinfo *ci, const char *oldteam, const char *newteam) {}
		virtual void initclient(clientinfo *ci, packetbuf &p, bool connecting) {}
		virtual void update() {}
		virtual void reset(bool empty) {}
		virtual void intermission() {}
		virtual bool hidefrags() { return false; }
		virtual int getteamscore(const char *team) { return 0; }
		virtual void getteamscores(vector<teamscore> &scores) {}
		virtual bool extinfoteam(const char *team, ucharbuf &p) { return false; }
	};

	#define SERVMODE 1
	#include "capture.h"
	#include "ctf.h"

	captureservmode capturemode;
	ctfservmode ctfmode;
	servmode *smode = NULL;

	bool canspawnitem(int type) { return !m_noitems && (type>=I_SHELLS && type<=I_QUAD && (!m_noammo || type<I_SHELLS || type>I_CARTRIDGES)); }

	int spawntime(int type)
	{
		if(m_classicsp) return INT_MAX;
		int np = numclients(-1, true, false);
		np = np<3 ? 4 : (np>4 ? 2 : 3);         // spawn times are dependent on number of players
		int sec = 0;
		switch(type)
		{
			case I_SHELLS:
			case I_BULLETS:
			case I_ROCKETS:
			case I_ROUNDS:
			case I_GRENADES:
			case I_CARTRIDGES: sec = np*4; break;
			case I_HEALTH: sec = np*5; break;
			case I_GREENARMOUR:
			case I_YELLOWARMOUR: sec = 20; break;
			case I_BOOST:
			case I_QUAD: sec = 40+rnd(40); break;
		}
		return sec*1000;
	}

	bool delayspawn(int type)
	{
		switch(type)
		{
			case I_GREENARMOUR:
			case I_YELLOWARMOUR:
				return !m_classicsp;
			case I_BOOST:
			case I_QUAD:
				return true;
			default:
				return false;
		}
	}

	bool pickup(int i, int sender)         // server side item pickup, acknowledge first client that gets it
	{
		if((m_timed && gamemillis>=gamelimit) || !sents.inrange(i) || !sents[i].spawned) return false;
		clientinfo *ci = getinfo(sender);
		if(!ci || (!ci->local && !ci->state.canpickup(sents[i].type))) return false;
		sents[i].spawned = false;
		sents[i].spawntime = spawntime(sents[i].type);
		sendf(-1, 1, "ri3", N_ITEMACC, i, sender);
		ci->state.pickup(sents[i].type);
		return true;
	}

	clientinfo *choosebestclient(float &bestrank)
	{
		clientinfo *best = NULL;
		bestrank = -1;
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->state.timeplayed<0) continue;
			float rank = ci->state.state!=CS_SPECTATOR ? ci->state.effectiveness/max(ci->state.timeplayed, 1) : -1;
			if(!best || rank > bestrank) { best = ci; bestrank = rank; }
		}
		return best;
	}

	void autoteam()
	{
		static const char *teamnames[2] = {"good", "evil"};
		vector<clientinfo *> team[2];
		float teamrank[2] = {0, 0};
		for(int round = 0, remaining = clients.length(); remaining>=0; round++)
		{
			int first = round&1, second = (round+1)&1, selected = 0;
			while(teamrank[first] <= teamrank[second])
			{
				float rank;
				clientinfo *ci = choosebestclient(rank);
				if(!ci) break;
				if(smode && smode->hidefrags()) rank = 1;
				else if(selected && rank<=0) break;
				ci->state.timeplayed = -1;
				team[first].add(ci);
				if(rank>0) teamrank[first] += rank;
				selected++;
				if(rank<=0) break;
			}
			if(!selected) break;
			remaining -= selected;
		}
		loopi(sizeof(team)/sizeof(team[0]))
		{
			loopvj(team[i])
			{
				clientinfo *ci = team[i][j];
				if(!strcmp(ci->team, teamnames[i])) continue;
				copystring(ci->team, teamnames[i], MAXTEAMLEN+1);
				sendf(-1, 1, "riisi", N_SETTEAM, ci->clientnum, teamnames[i], -1);
			}
		}
	}

	struct teamrank
	{
		const char *name;
		float rank;
		int clients;

		teamrank(const char *name) : name(name), rank(0), clients(0) {}
	};

	const char *chooseworstteam(const char *suggest = NULL, clientinfo *exclude = NULL)
	{
		teamrank teamranks[2] = { teamrank("good"), teamrank("evil") };
		const int numteams = sizeof(teamranks)/sizeof(teamranks[0]);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci==exclude || ci->state.aitype!=AI_NONE || ci->state.state==CS_SPECTATOR || !ci->team[0]) continue;
			ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
			ci->state.lasttimeplayed = lastmillis;

			loopj(numteams) if(!strcmp(ci->team, teamranks[j].name))
			{
				teamrank &ts = teamranks[j];
				ts.rank += ci->state.effectiveness/max(ci->state.timeplayed, 1);
				ts.clients++;
				break;
			}
		}
		teamrank *worst = &teamranks[numteams-1];
		loopi(numteams-1)
		{
			teamrank &ts = teamranks[i];
			if(smode && smode->hidefrags())
			{
				if(ts.clients < worst->clients || (ts.clients == worst->clients && ts.rank < worst->rank)) worst = &ts;
			}
			else if(ts.rank < worst->rank || (ts.rank == worst->rank && ts.clients < worst->clients)) worst = &ts;
		}
		return worst->name;
	}

	void writedemo(int chan, void *data, int len)
	{
		if(!demorecord) return;
		int stamp[3] = { gamemillis, chan, len };
		lilswap(stamp, 3);
		demorecord->write(stamp, sizeof(stamp));
		demorecord->write(data, len);
	}

	void recordpacket(int chan, void *data, int len)
	{
		writedemo(chan, data, len);
	}

	void enddemorecord()
	{
		if(!demorecord) return;

		DELETEP(demorecord);

		if(!demotmp) return;

		int len = demotmp->size();
		if(demos.length()>=MAXDEMOS)
		{
			delete[] demos[0].data;
			demos.remove(0);
		}
		demofile &d = demos.add();
		time_t t = time(NULL);
		char *timestr = ctime(&t), *trim = timestr + strlen(timestr);
		while(trim>timestr && isspace(*--trim)) *trim = '\0';
		formatstring(d.info)("%s: %s, %s, %.2f%s", timestr, modename(gamemode), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
		defformatstring(msg)("demo \"%s\" recorded", d.info);
		outf(3, msg);
		d.data = new uchar[len];
		d.len = len;
		demotmp->seek(0, SEEK_SET);
		demotmp->read(d.data, len);
		DELETEP(demotmp);
	}

	int welcomepacket(packetbuf &p, clientinfo *ci);
	void sendwelcome(clientinfo *ci);

	void setupdemorecord()
	{
		if(!m_mp(gamemode) || m_edit) return;

		demotmp = opentempfile("demorecord", "w+b");
		if(!demotmp) return;

		stream *f = opengzfile(NULL, "wb", demotmp);
		if(!f) { DELETEP(demotmp); return; }

		outf(3, "recording demo");

		demorecord = f;

		demoheader hdr;
		memcpy(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic));
		hdr.version = DEMO_VERSION;
		hdr.protocol = PROTOCOL_VERSION;
		lilswap(&hdr.version, 2);
		demorecord->write(&hdr, sizeof(demoheader));

		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		welcomepacket(p, NULL);
		writedemo(1, p.buf, p.len);
	}

	void listdemos(int cn)
	{
		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		putint(p, N_SENDDEMOLIST);
		putint(p, demos.length());
		loopv(demos) sendstring(demos[i].info, p);
		sendpacket(cn, 1, p.finalize());
	}

	void cleardemos(int n)
	{
		if(!n)
		{
			loopv(demos) delete[] demos[i].data;
			demos.shrink(0);
			outf(3, "cleared all demos");
		}
		else if(demos.inrange(n-1))
		{
			delete[] demos[n-1].data;
			demos.remove(n-1);
			defformatstring(msg)("cleared demo %d", n);
			outf(3, msg);
		}
	}

	void senddemo(int cn, int num)
	{
		if(!num) num = demos.length();
		if(!demos.inrange(num-1)) return;
		demofile &d = demos[num-1];
		sendf(cn, 2, "rim", N_SENDDEMO, d.len, d.data);
	}

	void enddemoplayback()
	{
		if(!demoplayback) return;
		DELETEP(demoplayback);

		loopv(clients) sendf(clients[i]->clientnum, 1, "ri3", N_DEMOPLAYBACK, 0, clients[i]->clientnum);

		outf(3, "demo playback finished");

		loopv(clients) sendwelcome(clients[i]);
	}

	void setupdemoplayback()
	{
		if(demoplayback) return;
		demoheader hdr;
		string msg;
		msg[0] = '\0';
		defformatstring(file)("%s.dmo", smapname);
		demoplayback = opengzfile(file, "rb");
		if(!demoplayback) formatstring(msg)("could not read demo \"%s\"", file);
		else if(demoplayback->read(&hdr, sizeof(demoheader))!=sizeof(demoheader) || memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)))
			formatstring(msg)("\"%s\" is not a demo file", file);
		else
		{
			lilswap(&hdr.version, 2);
			if(hdr.version!=DEMO_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of Cube 2: Sauerbraten", file, hdr.version<DEMO_VERSION ? "older" : "newer");
			else if(hdr.protocol!=PROTOCOL_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of Cube 2: Sauerbraten", file, hdr.protocol<PROTOCOL_VERSION ? "older" : "newer");
		}
		if(msg[0])
		{
			DELETEP(demoplayback);
			outf(3, "%s", msg);
			return;
		}

		formatstring(msg)("playing demo \"%s\"", file);
		outf(3, "%s", msg);

		demomillis = 0;
		sendf(-1, 1, "ri3", N_DEMOPLAYBACK, 1, -1);

		if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
		{
			enddemoplayback();
			return;
		}
		lilswap(&nextplayback, 1);
	}

	void readdemo()
	{
		if(!demoplayback || gamepaused) return;
		demomillis += curtime;
		while(demomillis>=nextplayback)
		{
			int chan, len;
			if(demoplayback->read(&chan, sizeof(chan))!=sizeof(chan) ||
			   demoplayback->read(&len, sizeof(len))!=sizeof(len))
			{
				enddemoplayback();
				return;
			}
			lilswap(&chan, 1);
			lilswap(&len, 1);
			ENetPacket *packet = enet_packet_create(NULL, len, 0);
			if(!packet || demoplayback->read(packet->data, len)!=len)
			{
				if(packet) enet_packet_destroy(packet);
				enddemoplayback();
				return;
			}
			sendpacket(-1, chan, packet);
			if(!packet->referenceCount) enet_packet_destroy(packet);
			if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
			{
				enddemoplayback();
				return;
			}
			lilswap(&nextplayback, 1);
		}
	}

	void stopdemo()
	{
		if(m_demo) enddemoplayback();
		else enddemorecord();
	}

	void pausegame(bool val)
	{
		if(gamepaused==val) return;
		gamepaused = val;
		sendf(-1, 1, "rii", N_PAUSEGAME, gamepaused ? 1 : 0);
	}

	void hashpassword(int cn, int sessionid, const char *pwd, char *result, int maxlen)
	{
		char buf[2*sizeof(string)];
		formatstring(buf)("%d %d ", cn, sessionid);
		copystring(&buf[strlen(buf)], pwd);
		if(!hashstring(buf, result, maxlen)) *result = '\0';
	}

	bool checkpassword(clientinfo *ci, const char *wanted, const char *given)
	{
		string hash;
		hashpassword(ci->clientnum, ci->sessionid, wanted, hash, sizeof(hash));
		return !strcmp(hash, given);
	}

	void revokemaster(clientinfo *ci)
	{
		ci->privilege = PRIV_NONE;
		if(ci->state.state==CS_SPECTATOR && !ci->local) aiman::removeai(ci);
	}

	bool checkblacklist(clientinfo *ci, char **reason) {
		loopv(blacklistips) {
			if(!fnmatch(blacklistips[i].pattern, ci->name, 0) ||
			   !fnmatch(blacklistips[i].pattern, getclienthostname(ci->clientnum), 0) ||
			   !fnmatch(blacklistips[i].pattern, getclientipstr(ci->clientnum), 0)) {
			   if(reason) *reason = blacklistips[i].reason;
				return true;
			}
		}
		return false;
	}

	void updateirctopic();
	void setmaster(clientinfo *ci, bool val, const char *pass, const char *authname, bool no_open)
	{
		if(authname && !val) return;
		const char *name = "";
		bool haspass = false;
		if(val) {
			haspass = adminpass[0] && checkpassword(ci, adminpass, pass);

			if(ci->privilege) {
				if(!adminpass[0] || haspass==(ci->privilege==PRIV_ADMIN)) return;
			} else if(ci->state.state==CS_SPECTATOR && !haspass && !authname && !ci->local) return;

			loopv(clients) if(ci!=clients[i] && clients[i]->privilege) {
				if(haspass) clients[i]->privilege = PRIV_NONE;
				else if((authname || ci->local) && clients[i]->privilege<=PRIV_MASTER) continue;
				else return;
			}

			if(haspass) ci->privilege = PRIV_ADMIN;
			else if(!authname && !(mastermask&MM_AUTOAPPROVE) && !ci->privilege && !ci->local) {
				sendf(ci->clientnum, 1, "ris", N_SERVMSG, "This server requires you to use the \"/auth\" command to gain master.");
				return;
			} else {
				char *reason = (char *)"";
				if(checkblacklist(ci, &reason)) {
					if(reason && reason[0]) {
						outf(2, "\f3setmaster denied for \f6%s\f3, reason: \"\f7%s\f3\"", colorname(ci, NULL, true), reason);
					} else {
						outf(2, "\f3setmaster denied for \f6%s\f3", colorname(ci, NULL, true));
					}
					return;
				}
				if(authname) {
					loopv(clients) if(ci!=clients[i] && clients[i]->privilege<=PRIV_MASTER) revokemaster(clients[i]);
				}
				ci->privilege = PRIV_MASTER;
			}

			name = privname(ci->privilege);
		} else {
			if(!ci->privilege) return;
			name = privname(ci->privilege);
			revokemaster(ci);
		}
		if(!no_open && (!haspass || !val)) mastermode = MM_OPEN;

		allowedips.shrink(0);
		string msg;
		if(val && authname) formatstring(msg)("%s claimed %s as '\f5%s\f7'", colorname(ci), name, authname);
		else formatstring(msg)("%s %s %s", colorname(ci), val ? "claimed" : "relinquished", name);
		outf(2, "%s. Mastermode is \f1%s\f7.", msg, mastermodename(mastermode));
		currentmaster = val ? ci->clientnum : -1;
		if(val) sendf(currentmaster, 1, "ris", N_SERVMSG, mastermessage);
		updateirctopic();
		sendf(-1, 1, "ri4", N_CURRENTMASTER, currentmaster, currentmaster >= 0 ? ci->privilege : 0, mastermode);
		if(gamepaused)
		{
			int admins = 0;
			loopv(clients) if(clients[i]->privilege >= PRIV_ADMIN || clients[i]->local) admins++;
			if(!admins) pausegame(false);
		}
	}

	savedscore &findscore(clientinfo *ci, bool insert)
	{
		uint ip = getclientip(ci->clientnum);
		if(!ip && !ci->local) return *(savedscore *)0;
		if(!insert)
		{
			loopv(clients)
			{
				clientinfo *oi = clients[i];
				if(oi->clientnum != ci->clientnum && getclientip(oi->clientnum) == ip && !strcmp(oi->name, ci->name))
				{
					oi->state.timeplayed += lastmillis - oi->state.lasttimeplayed;
					oi->state.lasttimeplayed = lastmillis;
					static savedscore curscore;
					curscore.save(oi->state);
					return curscore;
				}
			}
		}
		loopv(scores)
		{
			savedscore &sc = scores[i];
			if(sc.ip == ip && !strcmp(sc.name, ci->name)) return sc;
		}
		if(!insert) return *(savedscore *)0;
		savedscore &sc = scores.add();
		sc.ip = ip;
		copystring(sc.name, ci->name);
		return sc;
	}

	void savescore(clientinfo *ci)
	{
		savedscore &sc = findscore(ci, true);
		if(&sc) sc.save(ci->state);
	}

	int checktype(int type, clientinfo *ci)
	{
		if(ci && ci->local) return type;
		// only allow edit messages in coop-edit mode
		if(type>=N_EDITENT && type<=N_EDITVAR && !m_edit) return -1;
		// server only messages
		static const int servtypes[] = { N_SERVINFO, N_INITCLIENT, N_WELCOME, N_MAPRELOAD, N_SERVMSG, N_DAMAGE, N_HITPUSH, N_SHOTFX, N_EXPLODEFX, N_DIED, N_SPAWNSTATE, N_FORCEDEATH, N_ITEMACC, N_ITEMSPAWN, N_TIMEUP, N_CDIS, N_CURRENTMASTER, N_PONG, N_RESUME, N_BASESCORE, N_BASEINFO, N_BASEREGEN, N_ANNOUNCE, N_SENDDEMOLIST, N_SENDDEMO, N_DEMOPLAYBACK, N_SENDMAP, N_DROPFLAG, N_SCOREFLAG, N_RETURNFLAG, N_RESETFLAG, N_INVISFLAG, N_CLIENT, N_AUTHCHAL, N_INITAI };
		if(ci) 
		{
			loopi(sizeof(servtypes)/sizeof(int)) if(type == servtypes[i]) return -1;
			if(type < N_EDITENT || type > N_EDITVAR || !m_edit) 
			{
				if(type != N_POS && ++ci->overflow >= 200) return -2;
			}
		}
		return type;
	}

	void cleanworldstate(ENetPacket *packet)
	{
		loopv(worldstates)
		{
			worldstate *ws = worldstates[i];
			if(ws->positions.inbuf(packet->data) || ws->messages.inbuf(packet->data)) ws->uses--;
			else continue;
			if(!ws->uses)
			{
				delete ws;
				worldstates.remove(i);
			}
			break;
		}
	}

	void flushclientposition(clientinfo &ci)
	{
		if(ci.position.empty() || (!hasnonlocalclients() && !demorecord)) return;
		packetbuf p(ci.position.length(), 0);
		p.put(ci.position.getbuf(), ci.position.length());
		ci.position.setsize(0);
		sendpacket(-1, 0, p.finalize(), ci.ownernum);
	}

	void addclientstate(worldstate &ws, clientinfo &ci)
	{
		if(ci.position.empty()) ci.posoff = -1;
		else
		{
			ci.posoff = ws.positions.length();
			ws.positions.put(ci.position.getbuf(), ci.position.length());
			ci.poslen = ws.positions.length() - ci.posoff;
			ci.position.setsize(0);
		}
		if(ci.messages.empty()) ci.msgoff = -1;
		else
		{
			ci.msgoff = ws.messages.length();
			putint(ws.messages, N_CLIENT);
			putint(ws.messages, ci.clientnum);
			putuint(ws.messages, ci.messages.length());
			ws.messages.put(ci.messages.getbuf(), ci.messages.length());
			ci.msglen = ws.messages.length() - ci.msgoff;
			ci.messages.setsize(0);
		}
	}

	bool buildworldstate()
	{
		worldstate &ws = *new worldstate;
		loopv(clients)
		{
			clientinfo &ci = *clients[i];
			if(ci.state.aitype != AI_NONE) continue;
			ci.overflow = 0;
			addclientstate(ws, ci);
			loopv(ci.bots)
			{
				clientinfo &bi = *ci.bots[i];
				addclientstate(ws, bi);
				if(bi.posoff >= 0)
				{
					if(ci.posoff < 0) { ci.posoff = bi.posoff; ci.poslen = bi.poslen; }
					else ci.poslen += bi.poslen;
				}
				if(bi.msgoff >= 0)
				{
					if(ci.msgoff < 0) { ci.msgoff = bi.msgoff; ci.msglen = bi.msglen; }
					else ci.msglen += bi.msglen;
				}
			}
		}
		int psize = ws.positions.length(), msize = ws.messages.length();
		if(psize)
		{
			recordpacket(0, ws.positions.getbuf(), psize);
			ucharbuf p = ws.positions.reserve(psize);
			p.put(ws.positions.getbuf(), psize);
			ws.positions.addbuf(p);
		}
		if(msize)
		{
			recordpacket(1, ws.messages.getbuf(), msize);
			ucharbuf p = ws.messages.reserve(msize);
			p.put(ws.messages.getbuf(), msize);
			ws.messages.addbuf(p);
		}
		ws.uses = 0;
		if(psize || msize) loopv(clients)
		{
			clientinfo &ci = *clients[i];
			if(ci.state.aitype != AI_NONE) continue;
			ENetPacket *packet;
			if(psize && (ci.posoff<0 || psize-ci.poslen>0))
			{
				packet = enet_packet_create(&ws.positions[ci.posoff<0 ? 0 : ci.posoff+ci.poslen],
					                        ci.posoff<0 ? psize : psize-ci.poslen,
					                        ENET_PACKET_FLAG_NO_ALLOCATE);
				sendpacket(ci.clientnum, 0, packet);
				if(!packet->referenceCount) enet_packet_destroy(packet);
				else { ++ws.uses; packet->freeCallback = cleanworldstate; }
			}

			if(msize && (ci.msgoff<0 || msize-ci.msglen>0))
			{
				packet = enet_packet_create(&ws.messages[ci.msgoff<0 ? 0 : ci.msgoff+ci.msglen],
					                        ci.msgoff<0 ? msize : msize-ci.msglen,
					                        (reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0) | ENET_PACKET_FLAG_NO_ALLOCATE);
				sendpacket(ci.clientnum, 1, packet);
				if(!packet->referenceCount) enet_packet_destroy(packet);
				else { ++ws.uses; packet->freeCallback = cleanworldstate; }
			}
		}
		reliablemessages = false;
		if(!ws.uses)
		{
			delete &ws;
			return false;
		}
		else
		{
			worldstates.add(&ws);
			return true;
		}
	}

	bool sendpackets(bool force)
	{
		if(clients.empty() || (!hasnonlocalclients() && !demorecord)) return false;
		enet_uint32 curtime = enet_time_get()-lastsend;
		if(curtime<33 && !force) return false;
		bool flush = buildworldstate();
		lastsend += curtime - (curtime%33);
		return flush;
	}

	template<class T>
	void sendstate(gamestate &gs, T &p)
	{
		putint(p, gs.lifesequence);
		putint(p, gs.health);
		putint(p, gs.maxhealth);
		putint(p, gs.armour);
		putint(p, gs.armourtype);
		putint(p, gs.gunselect);
		loopi(GUN_PISTOL-GUN_SG+1) putint(p, gs.ammo[GUN_SG+i]);
	}

	void spawnstate(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		gs.spawnstate(gamemode);
		gs.lifesequence = (gs.lifesequence + 1)&0x7F;
	}

	void sendspawn(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		spawnstate(ci);
		sendf(ci->ownernum, 1, "rii7v", N_SPAWNSTATE, ci->clientnum, gs.lifesequence,
			gs.health, gs.maxhealth,
			gs.armour, gs.armourtype,
			gs.gunselect, GUN_PISTOL-GUN_SG+1, &gs.ammo[GUN_SG]);
		gs.lastspawn = gamemillis;
	}

	void sendwelcome(clientinfo *ci)
	{
		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		int chan = welcomepacket(p, ci);
		sendpacket(ci->clientnum, chan, p.finalize());
	}

	void putinitclient(clientinfo *ci, packetbuf &p)
	{
		if(ci->state.aitype != AI_NONE)
		{
			putint(p, N_INITAI);
			putint(p, ci->clientnum);
			putint(p, ci->ownernum);
			putint(p, ci->state.aitype);
			putint(p, ci->state.skill);
			putint(p, ci->playermodel);
			sendstring(ci->name, p);
			sendstring(ci->team, p);
		}
		else
		{
			putint(p, N_INITCLIENT);
			putint(p, ci->clientnum);
			sendstring(ci->name, p);
			sendstring(ci->team, p);
			putint(p, ci->playermodel);
		}
	}

	void welcomeinitclient(packetbuf &p, int exclude = -1)
	{
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(!ci->connected || ci->clientnum == exclude) continue;

			putinitclient(ci, p);
		}
	}

	int welcomepacket(packetbuf &p, clientinfo *ci)
	{
		int hasmap = (m_edit && (clients.length()>1 || (ci && ci->local))) || (smapname[0] && (!m_timed || gamemillis<gamelimit || (ci && ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || numclients(ci ? ci->clientnum : -1, true, true, true)));
		putint(p, N_WELCOME);
		putint(p, hasmap);
		if(hasmap)
		{
			putint(p, N_MAPCHANGE);
			sendstring(smapname, p);
			putint(p, gamemode);
			putint(p, notgotitems ? 1 : 0);
			if(!ci || (m_timed && smapname[0]))
			{
				putint(p, N_TIMEUP);
				putint(p, gamemillis < gamelimit && !interm ? max((gamelimit - gamemillis)/1000, 1) : 0);
			}
			if(!notgotitems)
			{
				putint(p, N_ITEMLIST);
				loopv(sents) if(sents[i].spawned)
				{
					putint(p, i);
					putint(p, sents[i].type);
				}
				putint(p, -1);
			}
		}
		if(currentmaster >= 0 || mastermode != MM_OPEN)
		{
			putint(p, N_CURRENTMASTER);
			putint(p, currentmaster);
			clientinfo *m = currentmaster >= 0 ? getinfo(currentmaster) : NULL;
			putint(p, m ? m->privilege : 0);
			putint(p, mastermode);
		}
		if(gamepaused)
		{
			putint(p, N_PAUSEGAME);
			putint(p, 1);
		}
		if(ci)
		{
			putint(p, N_SETTEAM);
			putint(p, ci->clientnum);
			sendstring(ci->team, p);
			putint(p, -1);
		}
		if(ci && (m_demo || m_mp(gamemode)) && ci->state.state!=CS_SPECTATOR)
		{
			if(smode && !smode->canspawn(ci, true))
			{
				ci->state.state = CS_DEAD;
				putint(p, N_FORCEDEATH);
				putint(p, ci->clientnum);
				sendf(-1, 1, "ri2x", N_FORCEDEATH, ci->clientnum, ci->clientnum);
			}
			else
			{
				gamestate &gs = ci->state;
				spawnstate(ci);
				putint(p, N_SPAWNSTATE);
				putint(p, ci->clientnum);
				sendstate(gs, p);
				gs.lastspawn = gamemillis;
			}
		}
		if(ci && ci->state.state==CS_SPECTATOR)
		{
			putint(p, N_SPECTATOR);
			putint(p, ci->clientnum);
			putint(p, 1);
			sendf(-1, 1, "ri3x", N_SPECTATOR, ci->clientnum, 1, ci->clientnum);
		}
		if(!ci || clients.length()>1)
		{
			putint(p, N_RESUME);
			loopv(clients)
			{
				clientinfo *oi = clients[i];
				if(ci && oi->clientnum==ci->clientnum) continue;
				putint(p, oi->clientnum);
				putint(p, oi->state.state);
				putint(p, oi->state.frags);
				putint(p, oi->state.flags);
				putint(p, oi->state.quadmillis);
				sendstate(oi->state, p);
			}
			putint(p, -1);
			welcomeinitclient(p, ci ? ci->clientnum : -1);
		}
		if(smode) smode->initclient(ci, p, true);
		return 1;
	}

	bool restorescore(clientinfo *ci)
	{
		//if(ci->local) return false;
		savedscore &sc = findscore(ci, false);
		if(&sc)
		{
			sc.restore(ci->state);
			return true;
		}
		return false;
	}

	void sendresume(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		sendf(-1, 1, "ri3i9vi", N_RESUME, ci->clientnum,
			gs.state, gs.frags, gs.flags, gs.quadmillis,
			gs.lifesequence,
			gs.health, gs.maxhealth,
			gs.armour, gs.armourtype,
			gs.gunselect, GUN_PISTOL-GUN_SG+1, &gs.ammo[GUN_SG], -1);
	}

	void sendinitclient(clientinfo *ci)
	{
		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		putinitclient(ci, p);
		sendpacket(-1, 1, p.finalize(), ci->clientnum);
	}

	void irctopic(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		char *s = bvprintf(fmt, ap);
		char *cs = color_sauer2irc(s);
		free(s);
		va_end(ap);
		for(unsigned int i = 0; i < irc.servers.size(); i++)
			for(unsigned int j = 0; j < irc.servers[i]->channels.size(); j++)
				irc.servers[i]->raw("TOPIC %s :%s\n", irc.servers[i]->channels[j]->name, cs);
		free(cs);
	}
	ICOMMAND(irctopic, "s", (char *t), irctopic("%s", t););

	void updateirctopic() {
		if(clients.length() == 0) irctopic("\f7%s\f7: empty", serverdesc);
		else {
			clientinfo *cm = currentmaster > -1 ? (clientinfo *)getclientinfo(currentmaster) : NULL;
			irctopic("\f7%s\f7: %s on %s%s%s\n", serverdesc, modename(gamemode), smapname[0]?smapname:"new map", cm?(cm->privilege==PRIV_ADMIN?", admin is ":", master is "):"", cm?colorname(cm, NULL, true):"");
		}
	}

	void changemap(const char *s, int mode)
	{
		stopdemo();
		pausegame(false);
		if(smode) smode->reset(false);
		aiman::clearai();

		mapreload = false;
		gamemode = mode;
		gamemillis = 0;
		gamelimit = (m_overtime ? 15 : 10)*60000;
		interm = 0;
		nextexceeded = 0;
		copystring(smapname, s);
		resetitems();
		notgotitems = true;
		scores.shrink(0);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
		}

		if(!m_mp(gamemode)) kicknonlocalclients(DISC_PRIVATE);

		if(m_teammode) autoteam();

		if(m_capture) smode = &capturemode;
		else if(m_ctf) smode = &ctfmode;
		else smode = NULL;
		if(smode) smode->reset(false);

		if(m_timed && smapname[0]) sendf(-1, 1, "ri2", N_TIMEUP, gamemillis < gamelimit && !interm ? max((gamelimit - gamemillis)/1000, 1) : 0);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			ci->mapchange();
			ci->state.lasttimeplayed = lastmillis;
			if(m_mp(gamemode) && ci->state.state!=CS_SPECTATOR) sendspawn(ci);
		}

		aiman::changemap();

		if(m_demo)
		{
			if(clients.length()) setupdemoplayback();
		}
		else if(demonextmatch)
		{
			demonextmatch = false;
			setupdemorecord();
		}
		updateirctopic();
	}

	struct votecount
	{
		char *map;
		int mode, count;
		votecount() {}
		votecount(char *s, int n) : map(s), mode(n), count(0) {}
	};

	void checkvotes(bool force = false)
	{
		vector<votecount> votes;
		int maxvotes = 0;
		loopv(clients)
		{
			clientinfo *oi = clients[i];
			if(oi->state.state==CS_SPECTATOR && !oi->privilege && !oi->local) continue;
			if(oi->state.aitype!=AI_NONE) continue;
			maxvotes++;
			if(!oi->mapvote[0]) continue;
			votecount *vc = NULL;
			loopvj(votes) if(!strcmp(oi->mapvote, votes[j].map) && oi->modevote==votes[j].mode)
			{
				vc = &votes[j];
				break;
			}
			if(!vc) vc = &votes.add(votecount(oi->mapvote, oi->modevote));
			vc->count++;
		}
		votecount *best = NULL;
		loopv(votes) if(!best || votes[i].count > best->count || (votes[i].count == best->count && rnd(2))) best = &votes[i];
		if(force || (best && best->count > maxvotes/2))
		{
			if(demorecord) enddemorecord();
			if(best && (best->count > (force ? 1 : maxvotes/2)))
			{
				outf(2, force ? "vote passed by default" : "vote passed by majority");
				sendf(-1, 1, "risii", N_MAPCHANGE, best->map, best->mode, 1);
				changemap(best->map, best->mode);
			}
			else
			{
				mapreload = true;
				if(clients.length()) sendf(-1, 1, "ri", N_MAPRELOAD);
			}
		}
	}

	void forcemap(const char *map, int mode)
	{
		stopdemo();
		if(hasnonlocalclients() && !mapreload)
		{
			defformatstring(msg)("local player forced %s on map %s", modename(mode), map);
			outf(2, msg);
		}
		sendf(-1, 1, "risii", N_MAPCHANGE, map, mode, 1);
		changemap(map, mode);
	}
	ICOMMAND(map, "si", (char *s, int *m), {
		if(s) forcemap(s, m?*m:gamemode);
	});
	ICOMMAND(mode, "i", (int *m), {
		if(m) forcemap(smapname, *m);
	});

	void vote(char *map, int reqmode, int sender)
	{
		clientinfo *ci = getinfo(sender);
		if(!ci || (ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || (!ci->local && !m_mp(reqmode))) return;
		copystring(ci->mapvote, map);
		ci->modevote = reqmode;
		if(!ci->mapvote[0]) return;
		if(ci->local || mapreload || (ci->privilege && mastermode>=MM_VETO))
		{
			if(demorecord) enddemorecord();
			if((!ci->local || hasnonlocalclients()) && !mapreload)
			{
				defformatstring(msg)("%s \f1%s\f7 forced \f2%s\f7 on map \f6%s\f7", ci->privilege && mastermode>=MM_VETO ? privname(ci->privilege) : "local player", ci->name, modename(ci->modevote), ci->mapvote);
				outf(2, msg);
			}
			sendf(-1, 1, "risii", N_MAPCHANGE, ci->mapvote, ci->modevote, 1);
			changemap(ci->mapvote, ci->modevote);
		}
		else
		{
			defformatstring(msg)("%s suggests %s on map %s (select map to vote)", colorname(ci), modename(reqmode), map);
			outf(2, msg);
			checkvotes();
		}
	}

	void checkintermission()
	{
		if(gamemillis >= gamelimit && !interm)
		{
			sendf(-1, 1, "ri2", N_TIMEUP, 0);
			if(smode) smode->intermission();
			interm = gamemillis + 10000;
			http_post_event_intermission();
		}
	}

	void startintermission() { gamelimit = min(gamelimit, gamemillis); checkintermission(); }

	void dodamage(clientinfo *target, clientinfo *actor, int damage, int gun, const vec &hitpush = vec(0, 0, 0))
	{
		gamestate &ts = target->state;
		ts.dodamage(damage);
		actor->state.damage += damage;
		sendf(-1, 1, "ri6", N_DAMAGE, target->clientnum, actor->clientnum, damage, ts.armour, ts.health);
		if(target==actor) target->setpushed();
		else if(target!=actor && !hitpush.iszero())
		{
			ivec v = vec(hitpush).rescale(DNF);
			sendf(ts.health<=0 ? -1 : target->ownernum, 1, "ri7", N_HITPUSH, target->clientnum, gun, damage, v.x, v.y, v.z);
			target->setpushed();
		}
		if(ts.health<=0)
		{
			if(actor->clientnum != target->clientnum) {
				http_post_event_kill(actor, target, gun);
			}
			else
			{
				http_post_event_suicide(actor);
			}

			target->state.deaths++;
			if(actor!=target && isteam(actor->team, target->team)) {
				actor->state.teamkills++;
				outf(2 | OUT_NOGAME, "%s fragged a teammate (%s)", colorname(actor), colorname(target));
			}
			int fragvalue = smode ? smode->fragvalue(target, actor) : (target==actor || isteam(target->team, actor->team) ? -1 : 1);
			actor->state.frags += fragvalue;
			if(fragvalue>0)
			{
				int friends = 0, enemies = 0; // note: friends also includes the fragger
				if(m_teammode) loopv(clients) if(strcmp(clients[i]->team, actor->team)) enemies++; else friends++;
				else { friends = 1; enemies = clients.length()-1; }
				actor->state.effectiveness += fragvalue*friends/float(max(enemies, 1));
			}
			sendf(-1, 1, "ri4", N_DIED, target->clientnum, actor->clientnum, actor->state.frags);
			target->position.setsize(0);
			if(smode) smode->died(target, actor);
			ts.state = CS_DEAD;
			ts.lastdeath = gamemillis;
			// don't issue respawn yet until DEATHMILLIS has elapsed
			// ts.respawn();
		}
	}

	void suicide(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		if(gs.state!=CS_ALIVE) return;
		ci->state.frags += smode ? smode->fragvalue(ci, ci) : -1;
		ci->state.deaths++;

		http_post_event_suicide(ci);

		sendf(-1, 1, "ri4", N_DIED, ci->clientnum, ci->clientnum, gs.frags);
		ci->position.setsize(0);
		if(smode) smode->died(ci, NULL);
		gs.state = CS_DEAD;
		gs.respawn();
	}

	void suicideevent::process(clientinfo *ci)
	{
		suicide(ci);
	}

	void explodeevent::process(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		switch(gun)
		{
			case GUN_RL:
				if(!gs.rockets.remove(id)) return;
				break;

			case GUN_GL:
				if(!gs.grenades.remove(id)) return;
				break;

			default:
				return;
		}
		sendf(-1, 1, "ri4x", N_EXPLODEFX, ci->clientnum, gun, id, ci->ownernum);
		loopv(hits)
		{
			hitinfo &h = hits[i];
			clientinfo *target = getinfo(h.target);
			if(!target || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.dist<0 || h.dist>RL_DAMRAD) continue;

			bool dup = false;
			loopj(i) if(hits[j].target==h.target) { dup = true; break; }
			if(dup) continue;

			int damage = guns[gun].damage;
			if(gs.quadmillis) damage *= 4;
			damage = int(damage*(1-h.dist/RL_DISTSCALE/RL_DAMRAD));
			if(gun==GUN_RL && target==ci) damage /= RL_SELFDAMDIV;
			dodamage(target, ci, damage, gun, h.dir);
		}
	}

	void shotevent::process(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		int wait = millis - gs.lastshot;
		if(!gs.isalive(gamemillis) ||
		   wait<gs.gunwait ||
		   gun<GUN_FIST || gun>GUN_PISTOL ||
		   gs.ammo[gun]<=0 || (guns[gun].range && from.dist(to) > guns[gun].range + 1))
			return;
		if(gun!=GUN_FIST) gs.ammo[gun]--;
		gs.lastshot = millis;
		gs.gunwait = guns[gun].attackdelay;
		sendf(-1, 1, "rii9x", N_SHOTFX, ci->clientnum, gun, id,
				int(from.x*DMF), int(from.y*DMF), int(from.z*DMF),
				int(to.x*DMF), int(to.y*DMF), int(to.z*DMF),
				ci->ownernum);
		gs.shotdamage += guns[gun].damage*(gs.quadmillis ? 4 : 1)*(gun==GUN_SG ? SGRAYS : 1);
		switch(gun)
		{
			case GUN_RL: gs.rockets.add(id); break;
			case GUN_GL: gs.grenades.add(id); break;
			default:
			{
				int totalrays = 0, maxrays = gun==GUN_SG ? SGRAYS : 1;
				loopv(hits)
				{
					hitinfo &h = hits[i];
					clientinfo *target = getinfo(h.target);
					if(!target || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.rays<1 || h.dist > guns[gun].range + 1) continue;

					totalrays += h.rays;
					if(totalrays>maxrays) continue;
					int damage = h.rays*guns[gun].damage;
					if(gs.quadmillis) damage *= 4;
					dodamage(target, ci, damage, gun, h.dir);
				}
				break;
			}
		}
	}

	void pickupevent::process(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		if(m_mp(gamemode) && !gs.isalive(gamemillis)) return;
		pickup(ent, ci->clientnum);
	}

	bool gameevent::flush(clientinfo *ci, int fmillis)
	{
		process(ci);
		return true;
	}

	bool timedevent::flush(clientinfo *ci, int fmillis)
	{
		if(millis > fmillis) return false;
		else if(millis >= ci->lastevent)
		{
			ci->lastevent = millis;
			process(ci);
		}
		return true;
	}

	void clearevent(clientinfo *ci)
	{
		delete ci->events.remove(0);
	}

	void flushevents(clientinfo *ci, int millis)
	{
		while(ci->events.length())
		{
			gameevent *ev = ci->events[0];
			if(ev->flush(ci, millis)) clearevent(ci);
			else break;
		}
	}

	void processevents()
	{
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(curtime>0 && ci->state.quadmillis) ci->state.quadmillis = max(ci->state.quadmillis-curtime, 0);
			flushevents(ci, gamemillis);
		}
	}

	void cleartimedevents(clientinfo *ci)
	{
		int keep = 0;
		loopv(ci->events)
		{
			if(ci->events[i]->keepable())
			{
				if(keep < i)
				{
					for(int j = keep; j < i; j++) delete ci->events[j];
					ci->events.remove(keep, i - keep);
					i = keep;
				}
				keep = i+1;
				continue;
			}
		}
		while(ci->events.length() > keep) delete ci->events.pop();
		ci->timesync = false;
	}

	bool ispaused() { return gamepaused; }

	void serverupdate()
	{
		if(!gamepaused) gamemillis += curtime;

		if(m_demo) readdemo();
		else if(!gamepaused && (!m_timed || gamemillis < gamelimit))
		{
			processevents();
			if(curtime)
			{
				loopv(sents) if(sents[i].spawntime) // spawn entities when timer reached
				{
					int oldtime = sents[i].spawntime;
					sents[i].spawntime -= curtime;
					if(sents[i].spawntime<=0)
					{
					    sents[i].spawntime = 0;
					    sents[i].spawned = true;
					    sendf(-1, 1, "ri2", N_ITEMSPAWN, i);
					}
					else if(sents[i].spawntime<=10000 && oldtime>10000 && (sents[i].type==I_QUAD || sents[i].type==I_BOOST))
					{
					    sendf(-1, 1, "ri2", N_ANNOUNCE, sents[i].type);
					}
				}
			}
			aiman::checkai();
			if(smode) smode->update();
		}

		loopv(bannedips) if(bannedips[i].time != -1 && bannedips[i].time-totalmillis>4*60*60000) { bannedips.remove(i); i--; }
		loopv(connects) if(totalmillis-connects[i]->connectmillis>15000) disconnect_client(connects[i]->clientnum, DISC_TIMEOUT);

		if(nextexceeded && gamemillis > nextexceeded && (!m_timed || gamemillis < gamelimit))
		{
			nextexceeded = 0;
			loopvrev(clients) 
			{
				clientinfo &c = *clients[i];
				if(c.state.aitype != AI_NONE) continue;
				if(c.checkexceeded()) disconnect_client(c.clientnum, DISC_TAGT);
				else c.scheduleexceeded();
			}
		}

		if(!gamepaused && m_timed && smapname[0] && gamemillis-curtime>0) checkintermission();
		if(interm > 0 && gamemillis>interm)
		{
			if(demorecord) enddemorecord();
			interm = -1;
			checkvotes(true);
		}
	}

	struct crcinfo
	{
		int crc, matches;

		crcinfo(int crc, int matches) : crc(crc), matches(matches) {}

		static int compare(const crcinfo *x, const crcinfo *y)
		{
			if(x->matches > y->matches) return -1;
			if(x->matches < y->matches) return 1;
			return 0;
		}
	};

	void checkmaps(int req = -1)
	{
		if(m_edit || !smapname[0]) return;
		vector<crcinfo> crcs;
		int total = 0, unsent = 0, invalid = 0;
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->state.state==CS_SPECTATOR || ci->state.aitype != AI_NONE) continue;
			total++;
			if(!ci->clientmap[0])
			{
				if(ci->mapcrc < 0) invalid++;
				else if(!ci->mapcrc) unsent++;
			}
			else
			{
				crcinfo *match = NULL;
				loopvj(crcs) if(crcs[j].crc == ci->mapcrc) { match = &crcs[j]; break; }
				if(!match) crcs.add(crcinfo(ci->mapcrc, 1));
				else match->matches++;
			}
		}
		if(total - unsent < min(total, 4)) return;
		crcs.sort(crcinfo::compare);
		string msg;
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->state.state==CS_SPECTATOR || ci->state.aitype != AI_NONE || ci->clientmap[0] || ci->mapcrc >= 0 || (req < 0 && ci->warned)) continue;
			formatstring(msg)("%s has modified map \"%s\"", colorname(ci), smapname);
			sendf(req, 1, "ris", N_SERVMSG, msg);
			if(req < 0) ci->warned = true;
		}
		if(crcs.empty() || crcs.length() < 2) return;
		loopv(crcs)
		{
			crcinfo &info = crcs[i];
			if(i || info.matches <= crcs[i+1].matches) loopvj(clients)
			{
				clientinfo *ci = clients[j];
				if(ci->state.state==CS_SPECTATOR || ci->state.aitype != AI_NONE || !ci->clientmap[0] || ci->mapcrc != info.crc || (req < 0 && ci->warned)) continue;
				formatstring(msg)("%s has modified map \"%s\"", colorname(ci), smapname);
				sendf(req, 1, "ris", N_SERVMSG, msg);
				if(req < 0) ci->warned = true;
			}
		}
	}

	void sendservinfo(clientinfo *ci)
	{
		sendf(ci->clientnum, 1, "ri5s", N_SERVINFO, ci->clientnum, PROTOCOL_VERSION, ci->sessionid, serverpass[0] ? 1 : 0, serverdesc);
	}

	void clearbans();
	void noclients()
	{
		updateirctopic();
		clearbans();
		aiman::clearai();
	}

	void localconnect(int n)
	{
		clientinfo *ci = getinfo(n);
		ci->clientnum = ci->ownernum = n;
		ci->connectmillis = totalmillis;
		ci->sessionid = (rnd(0x1000000)*((totalmillis%10000)+1))&0xFFFFFF;
		ci->local = true;

		connects.add(ci);
		sendservinfo(ci);
	}

	void localdisconnect(int n)
	{
		if(m_demo) enddemoplayback();
		clientdisconnect(n);
	}

	int clientconnect(int n, uint ip)
	{
		clientinfo *ci = getinfo(n);
		ci->clientnum = ci->ownernum = n;
		ci->connectmillis = totalmillis;
		ci->sessionid = (rnd(0x1000000)*((totalmillis%10000)+1))&0xFFFFFF;

		connects.add(ci);
		if(!m_mp(gamemode)) return DISC_PRIVATE;
		sendservinfo(ci);
		return DISC_NONE;
	}

	void clientdisconnect(int n)
	{
		clientinfo *ci = getinfo(n);
		if(ci->connected)
		{
			if(ci->privilege) setmaster(ci, false);
			if(smode) smode->leavegame(ci, true);
			ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
			savescore(ci);
			outf(2 | OUT_NOGAME, "%s left", ci->name);
			defformatstring(mil)("%d", totalmillis - ci->connectmillis);
			http_post_event_disconnect(ci);
			sendf(-1, 1, "ri2", N_CDIS, n);
			clients.removeobj(ci);
			aiman::removeai(ci);
			if(!numclients(-1, false, true)) noclients(); // bans clear when server empties
		}
		else connects.removeobj(ci);
	}

	int reserveclients() { return 3; }

	struct gbaninfo
	{
		enet_uint32 ip, mask;
	};

	vector<gbaninfo> gbans;

	void cleargbans()
	{
		gbans.shrink(0);
	}

	bool checkgban(uint ip)
	{
		loopv(gbans) if((ip & gbans[i].mask) == gbans[i].ip) return true;
		return false;
	}

	void addgban(const char *name)
	{
		union { uchar b[sizeof(enet_uint32)]; enet_uint32 i; } ip, mask;
		ip.i = 0;
		mask.i = 0;
		loopi(4)
		{
			char *end = NULL;
			int n = strtol(name, &end, 10);
			if(!end) break;
			if(end > name) { ip.b[i] = n; mask.b[i] = 0xFF; }
			name = end;
			while(*name && *name++ != '.');
		}
		gbaninfo &ban = gbans.add();
		ban.ip = ip.i;
		ban.mask = mask.i;

		loopvrev(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->local || ci->privilege >= PRIV_ADMIN) continue;
			if(checkgban(getclientip(ci->clientnum))) disconnect_client(ci->clientnum, DISC_IPBAN);
		}
	}

	void addban(char *pattern, char *name, bool perm=false) {
		ban &b = bannedips.add();
		copystring(b.pattern, pattern);
		if(name) copystring(b.name, name);
		else b.name[0] = 0;
		b.time = perm?-1:totalmillis;
	}

	ICOMMAND(pban, "ss", (char *h, char *n), {
		if(h) {
			addban(h, n, true);
			outf(2, "Added permanent ban [%s] (%s)", h, n?n:"");
		}
	});

	ICOMMAND(ban, "ss", (char *h, char *n), {
	    if(h) addban(h, n);
	});

	void writepbans(stream *f) {
	    loopv(bannedips) {
		    if(bannedips[i].time == -1) {
				f->printf("pban [%s] [%s]\n", bannedips[i].pattern, bannedips[i].name);
		    }
	    }
	}

	ICOMMAND(unban, "s", (char *s), {
	    if(s && *s) loopv(bannedips) {
			if(!strcmp(bannedips[i].pattern, s)) {
				outf(2, "Ban %s (%s) was removed.", bannedips[i].pattern, bannedips[i].name);
				bannedips.remove(i); i--;
			}
	    }
	});

	void addblacklist(char *p, char *r) {
		if(p && *p) {
			black_ip &b = blacklistips.add();
			copystring(b.pattern, p);
			if(r && *r) copystring(b.reason, r);
			else b.reason[0] = 0;
			outf(2, "Added to blacklist: %s (%s)", p, r?r:"");
		}
	}
	ICOMMAND(blacklist, "ss", (char *p, char *r), {
		addblacklist(p, r);
	});
	ICOMMAND(unblacklist, "s", (char *p, char *r), {
	    if(p && *p) loopv(blacklistips) {
			if(!strcmp(blacklistips[i].pattern, p)) {
				outf(2, "Blacklist %s (%s) was removed.", blacklistips[i].pattern, blacklistips[i].reason);
				blacklistips.remove(i); i--;
			}
	    }
	});
	void writeblacklist(stream *f) {
		loopv(blacklistips) {
			f->printf("blacklist [%s] [%s]\n", blacklistips[i].pattern, blacklistips[i].reason);
		}
	}

	int allowconnect(clientinfo *ci, const char *pwd)
	{
		if(ci->local) return DISC_NONE;
		if(!m_mp(gamemode)) return DISC_PRIVATE;
		if(serverpass[0])
		{
			if(!checkpassword(ci, serverpass, pwd)) return DISC_PRIVATE;
			return DISC_NONE;
		}
		if(adminpass[0] && checkpassword(ci, adminpass, pwd)) return DISC_NONE;
		if(numclients(-1, false, true)>=maxclients) return DISC_MAXCLIENTS;
		uint ip = getclientip(ci->clientnum);
		const char *ipstr = getclientipstr(ci->clientnum);
		loopv(bannedips) {
			if(!fnmatch(bannedips[i].pattern, ipstr, 0)) return DISC_IPBAN;
		}
		if(checkgban(ip)) return DISC_IPBAN;
		if(mastermode>=MM_PRIVATE && allowedips.find(ip)<0) return DISC_PRIVATE;
		return DISC_NONE;
	}

	bool allowbroadcast(int n)
	{
		clientinfo *ci = getinfo(n);
		return ci && ci->connected;
	}

	clientinfo *findauth(uint id)
	{
		loopv(clients) if(clients[i]->authreq == id) return clients[i];
		return NULL;
	}

	void authfailed(uint id)
	{
		clientinfo *ci = findauth(id);
		if(!ci) return;
		ci->authreq = 0;
	}

	void authsucceeded(uint id)
	{
		clientinfo *ci = findauth(id);
		if(!ci) return;
		ci->authreq = 0;
		setmaster(ci, true, "", ci->authname);
	}

	void authchallenged(uint id, const char *val)
	{
		clientinfo *ci = findauth(id);
		if(!ci) return;
		sendf(ci->clientnum, 1, "risis", N_AUTHCHAL, "", id, val);
	}

	uint nextauthreq = 0;

	void tryauth(clientinfo *ci, const char *user)
	{
		if(!nextauthreq) nextauthreq = 1;
		ci->authreq = nextauthreq++;
		filtertext(ci->authname, user, false, 100);
		if(!requestmasterf("reqauth %u %s\n", ci->authreq, ci->authname))
		{
			ci->authreq = 0;
			sendf(ci->clientnum, 1, "ris", N_SERVMSG, "not connected to authentication server");
		}
	}

	void answerchallenge(clientinfo *ci, uint id, char *val)
	{
		if(ci->authreq != id) return;
		for(char *s = val; *s; s++)
		{
			if(!isxdigit(*s)) { *s = '\0'; break; }
		}
		if(!requestmasterf("confauth %u %s\n", id, val))
		{
			ci->authreq = 0;
			sendf(ci->clientnum, 1, "ris", N_SERVMSG, "not connected to authentication server");
		}
	}

	void processmasterinput(const char *cmd, int cmdlen, const char *args)
	{
		uint id;
		string val;
		if(sscanf(cmd, "failauth %u", &id) == 1)
			authfailed(id);
		else if(sscanf(cmd, "succauth %u", &id) == 1)
			authsucceeded(id);
		else if(sscanf(cmd, "chalauth %u %s", &id, val) == 2)
			authchallenged(id, val);
		else if(!strncmp(cmd, "cleargbans", cmdlen))
			cleargbans();
		else if(sscanf(cmd, "addgban %s", val) == 1)
			addgban(val);
	}

	void receivefile(int sender, uchar *data, int len)
	{
		if(!m_edit || len > 1024*1024) return;
		clientinfo *ci = getinfo(sender);
		if(ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) return;
		if(mapdata) DELETEP(mapdata);
		if(!len) return;
		mapdata = opentempfile("mapdata", "w+b");
		if(!mapdata) { sendf(sender, 1, "ris", N_SERVMSG, "failed to open temporary file for map"); return; }
		mapdata->write(data, len);
		defformatstring(msg)("[%s uploaded map to server, \"/getmap\" to receive it]", colorname(ci));
		outf(2, msg);
	}

	void sendclipboard(clientinfo *ci)
	{
		if(!ci->lastclipboard || !ci->clipboard) return;
		bool flushed = false;
		loopv(clients)
		{
			clientinfo &e = *clients[i];
			if(e.clientnum != ci->clientnum && e.needclipboard >= ci->lastclipboard) 
			{
				if(!flushed) { flushserver(true); flushed = true; }
				sendpacket(e.clientnum, 1, ci->clipboard);
			}
		}
	}

#ifdef HAVE_PROC
	ICOMMAND(getrss, "", (), {
		int64_t vmrss;
		proc_get_mem_usage(&vmrss, NULL);
		defformatstring(s)("%lldkB", vmrss);
		result(s);
	});
	ICOMMAND(getvsz, "", (), {
		int64_t vmsize;
		proc_get_mem_usage(NULL, &vmsize);
		defformatstring(s)("%lldkB", vmsize);
		result(s);
	});
#else
	ICOMMAND(getrss, "", (), result(""));
	ICOMMAND(getvsz, "", (), result(""));
#endif
	ICOMMAND(listclients, "", (), {
		vector<char> buf;
		string cn;
		loopv(clients) if(clients[i])
		{
			if(i > 0) buf.add(' ');
			formatstring(cn)("%d", clients[i]->clientnum);
			buf.put(cn, strlen(cn));
		}
		buf.add('\0');
		result(buf.getbuf());
	});
	ICOMMAND(getclientname, "i", (int *cn), {
		if(!cn) result("");
		clientinfo *ci = (clientinfo *)getclientinfo(*cn);
		result(ci ? ci->name : "");
	});
	ICOMMAND(getclientteam, "i", (int *cn), {
		if(!cn) result("");
		clientinfo *ci = (clientinfo *)getclientinfo(*cn);
		result(ci ? ci->team : "");
	});
	ICOMMAND(getclientnum, "s", (char *name), {
		if(name) {
			loopv(clients) if(clients[i]) {
				if(!strcasecmp(name, clients[i]->name)) intret(clients[i]->clientnum);
			}
		} else if(scriptclient) intret(scriptclient->clientnum);
	});
	ICOMMAND(getclientstate, "is", (int *cn, char *name), {
		clientinfo *ci = NULL;
		if(cn) ci = (clientinfo *)getclientinfo(*cn);
		if(ci) {
			if(!name || !name[0] || !strcmp(name, "state")) {
				intret(ci->state.state);
			} else if(!strcmp(name, "frags")) {
				intret(ci->state.frags);
			} else if(!strcmp(name, "deaths")) {
				intret(ci->state.deaths);
			} else if(!strcmp(name, "teamkills")) {
				intret(ci->state.teamkills);
			} else if(!strcmp(name, "damage")) {
				intret(ci->state.damage);
			}
		}
	});
	ICOMMAND(getclientip, "i", (int *cn), result(getclientipstr(*cn)));
	ICOMMAND(getclienthostname, "i", (int *cn), result(getclienthostname(*cn)));
#ifdef HAVE_GEOIP
	ICOMMAND(getclientcountry, "i", (int *cn), { const char *c = getclientcountry(*cn); result(c?c:""); });
#else
	ICOMMAND(getclientcountry, "i", (int *cn), result(""));
#endif
	ICOMMAND(getclientuptime, "i", (int *cn), { //FIXME: split into more functions (ie timestr)
		clientinfo *ci = (clientinfo *)getclientinfo(*cn);
		intret(ci ? totalmillis - ci->connectmillis : 0);
	});
	int ispriv(int cn, int minimum) {
		clientinfo *ci = (clientinfo *)getclientinfo(cn);
		if(ci && ci->privilege >= minimum) return 1;
		return 0;
	}
	ICOMMAND(ismaster, "i", (int *cn), intret(ispriv(*cn, PRIV_MASTER)));
	ICOMMAND(isadmin, "i", (int *cn), intret(ispriv(*cn, PRIV_ADMIN)));

	ICOMMAND(givemaster, "i", (int *cn), {
		if(cn) {
			clientinfo *ci = (clientinfo *)getclientinfo(*cn);
			if(ci) {
				clientinfo *cm = (clientinfo *)getclientinfo(currentmaster);
				if(cm) setmaster(cm, 0);
				setmaster(ci, 1, "", NULL, true);
			}
		}
	});

	ICOMMAND(takemaster, "", (), {
		clientinfo *cm = (clientinfo *)getclientinfo(currentmaster);
		if(cm) setmaster(cm, 0);
	});

	void kick_client(int victim, clientinfo *m) {
		clientinfo *ci = (clientinfo *)getclientinfo(victim);
		if(!ci) return;
		if(m && m->privilege < PRIV_ADMIN) {
			if(m->lastkickmillis && totalmillis - m->lastkickmillis <= kickmillis) {
				m->nkicks++;
				if(m->nkicks >= maxkicks) {
					defformatstring(foo)("Mass kicking (%s(%s) automatically added).", colorname(m), getclientipstr(m->clientnum), colorname(ci), getclientipstr(victim));
					addblacklist((char *)getclientipstr(m->clientnum), (char *)foo);
					clearbans();
					kick_client(m->clientnum, NULL);
				} else outf(2, "\f3Kick protection triggered: %s/%s tried to kick %s.", colorname(m), getclientipstr(m->clientnum), colorname(ci));
				m->lastkickmillis = totalmillis;
				return;
			} else m->nkicks = 0;
			m->lastkickmillis = totalmillis;
		}
		if(ci) { // no bots
			ban &b = bannedips.add();
			b.time = totalmillis;
			b.pattern[0] = 0;
			copystring(b.pattern, getclientipstr(victim));
			allowedips.removeobj(getclientip(victim));
			if(m) http_post_event_kick(m, ci);
			disconnect_client(victim, DISC_KICK);
		}
	}
	ICOMMAND(kick, "i", (int *cn), { if(cn) kick_client(*cn); });

	void spectator(int val, int spectator) {
		clientinfo *spinfo = (clientinfo *)getclientinfo(spectator); // no bots
		if(!spinfo || (spinfo->state.state==CS_SPECTATOR ? val : !val)) return;

		if(spinfo->state.state!=CS_SPECTATOR && val) {
			if(spinfo->state.state==CS_ALIVE) suicide(spinfo);
			if(smode) smode->leavegame(spinfo);
			spinfo->state.state = CS_SPECTATOR;
			spinfo->state.timeplayed += lastmillis - spinfo->state.lasttimeplayed;
			if(!spinfo->local && !spinfo->privilege) aiman::removeai(spinfo);
			outf(2, "\f0%s\f7 is now a spectator", colorname(spinfo));
		} else if(spinfo->state.state==CS_SPECTATOR && !val) {
			spinfo->state.state = CS_DEAD;
			spinfo->state.respawn();
			spinfo->state.lasttimeplayed = lastmillis;
			aiman::addclient(spinfo);
			if(spinfo->clientmap[0] || spinfo->mapcrc) checkmaps();
			sendf(-1, 1, "ri", N_MAPRELOAD);
			outf(2, "\f0%s\f7 is no longer a spectator", colorname(spinfo));
		}
		sendf(-1, 1, "ri3", N_SPECTATOR, spectator, val);
		if(!val && mapreload && !spinfo->privilege && !spinfo->local) sendf(spectator, 1, "ri", N_MAPRELOAD);
	}
	ICOMMAND(spectator, "ii", (int *v, int *s), { if(s && v) spectator(*v, *s); });

	void clearbans() {
		loopv(bannedips) {
			if(bannedips[i].time != -1) { bannedips.remove(i); i--; }
		}
		outf(2, "All bans cleared.");
	}
	COMMAND(clearbans, "");

	void setmastermode(int mm) {
		mastermode = mm;
		outf(2 | OUT_NOGAME, "\f4Mastermode is now \f5%s\f4 (%d)", mastermodename(mastermode), mastermode);
		allowedips.shrink(0);
		if(mm>=MM_PRIVATE) {
			loopv(clients) allowedips.add(getclientip(clients[i]->clientnum));
		}
		sendf(-1, 1, "rii", N_MASTERMODE, mastermode);
	}
	ICOMMAND(mastermode, "i", (int *m), { if(m) setmastermode(*m); });

	void whisper(int cn, const char *fmt, ...) {
		clientinfo *ci = (clientinfo *)getclientinfo(cn);
		if(!ci) return;
		va_list(ap);
		va_start(ap, fmt);
		string str;
		vsnprintf(str, MAXSTRLEN, fmt, ap);
		va_end(ap);
		sendf(cn, 1, "ris", N_SERVMSG, str);
	}

	ICOMMAND(echo, "C", (char *s), {
		if(httpoutbuf) evbuffer_add_printf(httpoutbuf, "%s", s);
		else if(scriptclient) whisper(scriptclient->clientnum, "%s", s);
		else if(scriptircsource) {
			char *cs = color_sauer2irc(s);
			scriptircsource->reply("%s", cs);
			free(cs);
		} else {
			char *cs = color_sauer2console(s);
			puts(cs);
			free(cs);
		}
	});

	struct allowedcommand {
		string cmd;
		int nparams;
		int privilege;
	};
	vector <allowedcommand> allowedcommands;
	void allowcommand(char *cmd, int nparams, int privilege) {
		allowedcommand &c = allowedcommands.add();
		copystring(c.cmd, cmd);
		c.nparams = nparams;
		c.privilege = privilege;
	}
	ICOMMAND(allowcommand, "sii", (char *c, int *np, int *pv), {
		if(c && *c) allowcommand(c, np?*np:0, pv?*pv:0); // default allow with zero params for everyone
	});

	void add_escaped_cubechar(evbuffer *buf, char c) {
		if(strchr("\"^", c))
			evbuffer_add_printf(buf, "^%c", c);
		else evbuffer_add_printf(buf, "%c", c);
	}

	void processcommand(char *txt, int privilege) {
		if(privilege < 2) {
			string cmd;
			char *c = txt;
			char *d = cmd;
			*d = 0;
			while(c - txt < MAXSTRLEN-1 && *c && !isspace(*c)) { *(d++) = *(c++); *d = 0; }
			loopv(allowedcommands) {
				if(!strcmp(cmd, allowedcommands[i].cmd) && allowedcommands[i].privilege <= privilege) {
					evbuffer *buf = evbuffer_new();
					evbuffer_add_printf(buf, "%s ", cmd);
					int state = 0; // 0 = outside param; 1 = inside param; 2 = inside quoted param
					int nparams = 0;
					while(*c && (nparams < allowedcommands[i].nparams || allowedcommands[i].nparams < 0)) {
						switch(state) {
							case 0: // outside param
								if(!isspace(*c)) {
									evbuffer_add_printf(buf, "\"");
									if(*c == '"') {
										state = 2; // entered quotes
									} else {
										state = 1; // entered param without quotes (no spaces)
										add_escaped_cubechar(buf, *c);
									}
								}
								break;
							case 1: // inside unquoted param
								if(isspace(*c)) { // end of param
									state = 0;
									evbuffer_add_printf(buf, "\" ");
									nparams++;
								} else add_escaped_cubechar(buf, *c);
								break;
							case 2: // inside quoted param
								if(*c == '^') state = 3; // inside escape
								else if(*c == '"') { // end of param
									state = 0;
									evbuffer_add_printf(buf, "\" ");
									nparams++;
								} else {
									add_escaped_cubechar(buf, *c);
								}
								break;
							case 3: // inside escaped char
								add_escaped_cubechar(buf, *c); // no big deal
								state = 2; // back to quotes mode
								break;
						}
						c++;
					}
					if(state > 0) evbuffer_add_printf(buf, "\"");
					char *execthis; int len = evbuffer_get_length(buf);
					if(len) {
						execthis = newstring(len + 1);
						evbuffer_remove(buf, execthis, len);
						execthis[len] = 0;
						execute(execthis);
						delete[] execthis;
					}
					evbuffer_free(buf);
					break;
				}
			}
		} else execute(txt);
	}

	bool processtext(clientinfo *ci, char *text) {
		if(!text || !*text) return false;
		if(NULL == strchr(frogchar, text[0])) {
			outf(1 | OUT_NOGAME, "\f1<%s> \f0%s", ci->name, text);
			return true;
		}
		scriptclient = ci;
		processcommand(text+1, ci->logged_in?PRIV_ADMIN:ci->privilege);
		scriptclient = NULL;
		return false;
	}

	ICOMMAND(me, "C", (char *s), {
		if(scriptclient) outf(1, "\f1* %s \f0%s", scriptclient->name, s);
	});
	ICOMMAND(whisper, "iC", (int *cn, char *s), {
		if(cn && s) if(scriptclient || scriptircsource) {
			char *ns = s;
			while(*ns && !isspace(*ns)) ns++;
			while(isspace(*ns)) ns++;
			whisper(*cn, "%s whispers: %s", scriptclient ? scriptclient->name : scriptircsource->peer->nick, ns);
		}
	});
	void sendmap(clientinfo *ci) {
		if(mapdata)
		{
			outf(2, "Sending map to %s...", ci->name);
			sendfile(ci->clientnum, 2, mapdata, "ri", N_SENDMAP);
			ci->needclipboard = totalmillis;
		}
		else whisper(ci->clientnum, "No map to send.");
	}
	ICOMMAND(sendto, "i", (int *cn), {
		if(cn) {
			clientinfo *ci = (clientinfo *)getclientinfo(*cn);
			if(ci) sendmap(ci);
		}
	});

	void parsepacket(int sender, int chan, packetbuf &p)     // has to parse exactly each byte of the packet
	{
		if(sender<0) return;
		char text[MAXTRANS];
		int type;
		clientinfo *ci = sender>=0 ? getinfo(sender) : NULL, *cq = ci, *cm = ci;
		if(ci && !ci->connected)
		{
			if(chan==0) return;
			else if(chan!=1 || getint(p)!=N_CONNECT) { disconnect_client(sender, DISC_TAGT); return; }
			else
			{
				getstring(text, p);
				filtertext(text, text, false, MAXNAMELEN);
				if(!text[0]) copystring(text, "unnamed");
				copystring(ci->name, text, MAXNAMELEN+1);

				getstring(text, p);
				int disc = allowconnect(ci, text);
				if(disc)
				{
					disconnect_client(sender, disc);
					return;
				}

				ci->playermodel = getint(p);

				if(m_demo) enddemoplayback();

				connects.removeobj(ci);
				clients.add(ci);

				ci->connected = true;
				ci->needclipboard = totalmillis;
				if(mastermode>=MM_LOCKED) ci->state.state = CS_SPECTATOR;
				ci->state.lasttimeplayed = lastmillis;

				const char *worst = m_teammode ? chooseworstteam(NULL, ci) : NULL;
				copystring(ci->team, worst ? worst : "good", MAXTEAMLEN+1);

				sendwelcome(ci);
				if(restorescore(ci)) sendresume(ci);
				sendinitclient(ci);

				aiman::addclient(ci);

				if(m_demo) setupdemoplayback();

				if(servermotd[0]) sendf(sender, 1, "ris", N_SERVMSG, servermotd);

#ifdef HAVE_GEOIP
				const char *country = getclientcountrynul(ci->clientnum);
				if(country) {
					outf(2 | OUT_NOGAME, "\f0%s\f7 connected from \f2%s\f7 (%s/%s)", ci->name, country, getclientipstr(ci->clientnum), getclienthostname(ci->clientnum));
					outf(2 | OUT_NOIRC | OUT_NOCONSOLE, "\f0%s\f7 is connected from \f2%s\f7", ci->name, country);
				} else // fall through
#endif
				outf(2 | OUT_NOGAME, "\f0%s\f7 connected (%s/%s)\n", ci->name, getclientipstr(ci->clientnum), getclienthostname(ci->clientnum));

				char *reason = (char *)"";
				if(checkblacklist(ci, &reason) && !ci->warned_blacklisted && reason && reason[0]) {
					outf(2, "\f3WARNING: Player \"\f6%s\f3\" is blacklisted: \"\f7%s\f3\".", colorname(ci, NULL, true), reason);
					ci->warned_blacklisted = true;
				}

				http_post_event_connect(ci);
			}
		}
		else if(chan==2)
		{
			receivefile(sender, p.buf, p.maxlen);
			return;
		}

		if(p.packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;
		#define QUEUE_AI clientinfo *cm = cq;
		#define QUEUE_MSG { if(cm && (!cm->local || demorecord || hasnonlocalclients())) while(curmsg<p.length()) cm->messages.add(p.buf[curmsg++]); }
		#define QUEUE_BUF(body) { \
			if(cm && (!cm->local || demorecord || hasnonlocalclients())) \
			{ \
				curmsg = p.length(); \
				{ body; } \
			} \
		}
		#define QUEUE_INT(n) QUEUE_BUF(putint(cm->messages, n))
		#define QUEUE_UINT(n) QUEUE_BUF(putuint(cm->messages, n))
		#define QUEUE_STR(text) QUEUE_BUF(sendstring(text, cm->messages))
		int curmsg;
		while((curmsg = p.length()) < p.maxlen) switch(type = checktype(getint(p), ci))
		{
			case N_POS:
			{
				int pcn = getuint(p); 
				p.get(); 
				uint flags = getuint(p);
				clientinfo *cp = getinfo(pcn);
				if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
				vec pos;
				loopk(3)
				{
					int n = p.get(); n |= p.get()<<8; if(flags&(1<<k)) { n |= p.get()<<16; if(n&0x800000) n |= -1<<24; }
					pos[k] = n/DMF;
				}
				loopk(3) p.get();
				int mag = p.get(); if(flags&(1<<3)) mag |= p.get()<<8;
				int dir = p.get(); dir |= p.get()<<8;
				vec vel = vec((dir%360)*RAD, (clamp(dir/360, 0, 180)-90)*RAD).mul(mag/DVELF);
				if(flags&(1<<4))
				{
					p.get(); if(flags&(1<<5)) p.get();
					if(flags&(1<<6)) loopk(2) p.get();
				}
				if(cp)
				{
					if((!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
					{
					    if(!ci->local && !m_edit && max(vel.magnitude2(), (float)fabs(vel.z)) >= 180)
					        cp->setexceeded();
					    cp->position.setsize(0);
					    while(curmsg<p.length()) cp->position.add(p.buf[curmsg++]);
					}
					if(smode && cp->state.state==CS_ALIVE) smode->moved(cp, cp->state.o, cp->gameclip, pos, (flags&0x80)!=0);
					cp->state.o = pos;
					cp->gameclip = (flags&0x80)!=0;
				}
				break;
			}

			case N_TELEPORT:
			{
				int pcn = getint(p), teleport = getint(p), teledest = getint(p);
				clientinfo *cp = getinfo(pcn);
				if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
				if(cp && (!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
				{
					flushclientposition(*cp);
					sendf(-1, 0, "ri4x", N_TELEPORT, pcn, teleport, teledest, cp->ownernum); 
				}
				break;
			}

			case N_JUMPPAD:
			{
				int pcn = getint(p), jumppad = getint(p);
				clientinfo *cp = getinfo(pcn);
				if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
				if(cp && (!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
				{
					cp->setpushed();
					flushclientposition(*cp);
					sendf(-1, 0, "ri3x", N_JUMPPAD, pcn, jumppad, cp->ownernum);
				}
				break;
			}

			case N_FROMAI:
			{
				int qcn = getint(p);
				if(qcn < 0) cq = ci;
				else
				{
					cq = getinfo(qcn);
					if(cq && qcn != sender && cq->ownernum != sender) cq = NULL;
				}
				break;
			}

			case N_EDITMODE:
			{
				int val = getint(p);
				if(!ci->local && !m_edit) break;
				if(val ? ci->state.state!=CS_ALIVE && ci->state.state!=CS_DEAD : ci->state.state!=CS_EDITING) break;
				if(smode)
				{
					if(val) smode->leavegame(ci);
					else smode->entergame(ci);
				}
				if(val)
				{
					ci->state.editstate = ci->state.state;
					ci->state.state = CS_EDITING;
					ci->events.setsize(0);
					ci->state.rockets.reset();
					ci->state.grenades.reset();
				}
				else ci->state.state = ci->state.editstate;
				QUEUE_MSG;
				break;
			}

			case N_MAPCRC:
			{
				getstring(text, p);
				int crc = getint(p);
				if(!ci) break;
				if(strcmp(text, smapname))
				{
					if(ci->clientmap[0])
					{
					    ci->clientmap[0] = '\0';
					    ci->mapcrc = 0;
					}
					else if(ci->mapcrc > 0) ci->mapcrc = 0;
					break;
				}
				copystring(ci->clientmap, text);
				ci->mapcrc = text[0] ? crc : 1;
				checkmaps();
				break;
			}

			case N_CHECKMAPS:
				checkmaps(sender);
				break;

			case N_TRYSPAWN:
				if(!ci || !cq || cq->state.state!=CS_DEAD || cq->state.lastspawn>=0 || (smode && !smode->canspawn(cq))) break;
				if(!ci->clientmap[0] && !ci->mapcrc)
				{
					ci->mapcrc = -1;
					checkmaps();
				}
				if(cq->state.lastdeath)
				{
					flushevents(cq, cq->state.lastdeath + DEATHMILLIS);
					cq->state.respawn();
				}
				cleartimedevents(cq);
				sendspawn(cq);
				break;

			case N_GUNSELECT:
			{
				int gunselect = getint(p);
				if(!cq || cq->state.state!=CS_ALIVE || gunselect<GUN_FIST || gunselect>GUN_PISTOL) break;
				cq->state.gunselect = gunselect;
				QUEUE_AI;
				QUEUE_MSG;
				break;
			}

			case N_SPAWN:
			{
				int ls = getint(p), gunselect = getint(p);
				if(!cq || (cq->state.state!=CS_ALIVE && cq->state.state!=CS_DEAD) || ls!=cq->state.lifesequence || cq->state.lastspawn<0) break;
				cq->state.lastspawn = -1;
				cq->state.state = CS_ALIVE;
				cq->state.gunselect = gunselect;
				cq->exceeded = 0;
				if(smode) smode->spawned(cq);
				QUEUE_AI;
				QUEUE_BUF({
					putint(cm->messages, N_SPAWN);
					sendstate(cq->state, cm->messages);
				});
				break;
			}

			case N_SUICIDE:
			{
				if(cq) cq->addevent(new suicideevent);
				break;
			}

			case N_SHOOT:
			{
				shotevent *shot = new shotevent;
				shot->id = getint(p);
				shot->millis = cq ? cq->geteventmillis(gamemillis, shot->id) : 0;
				shot->gun = getint(p);
				loopk(3) shot->from[k] = getint(p)/DMF;
				loopk(3) shot->to[k] = getint(p)/DMF;
				int hits = getint(p);
				loopk(hits)
				{
					if(p.overread()) break;
					hitinfo &hit = shot->hits.add();
					hit.target = getint(p);
					hit.lifesequence = getint(p);
					hit.dist = getint(p)/DMF;
					hit.rays = getint(p);
					loopk(3) hit.dir[k] = getint(p)/DNF;
				}
				if(cq) 
				{
					cq->addevent(shot);
					cq->setpushed();
				}
				else delete shot;
				break;
			}

			case N_EXPLODE:
			{
				explodeevent *exp = new explodeevent;
				int cmillis = getint(p);
				exp->millis = cq ? cq->geteventmillis(gamemillis, cmillis) : 0;
				exp->gun = getint(p);
				exp->id = getint(p);
				int hits = getint(p);
				loopk(hits)
				{
					if(p.overread()) break;
					hitinfo &hit = exp->hits.add();
					hit.target = getint(p);
					hit.lifesequence = getint(p);
					hit.dist = getint(p)/DMF;
					hit.rays = getint(p);
					loopk(3) hit.dir[k] = getint(p)/DNF;
				}
				if(cq) cq->addevent(exp);
				else delete exp;
				break;
			}

			case N_ITEMPICKUP:
			{
				int n = getint(p);
				if(!cq) break;
				pickupevent *pickup = new pickupevent;
				pickup->ent = n;
				cq->addevent(pickup);
				break;
			}

			case N_TEXT:
			{
				getstring(text, p);
				filtertext(text, text);

				if(processtext(ci, text)) {
					QUEUE_AI;
					QUEUE_INT(N_TEXT);
					QUEUE_STR(text);
				}
				break;
			}

			case N_SAYTEAM:
			{
				getstring(text, p);
				if(!ci || !cq || (ci->state.state==CS_SPECTATOR && !ci->local && !ci->privilege) || !m_teammode || !cq->team[0]) break;
				loopv(clients)
				{
					clientinfo *t = clients[i];
					if(t==cq || t->state.state==CS_SPECTATOR || t->state.aitype != AI_NONE || strcmp(cq->team, t->team)) continue;
					sendf(t->clientnum, 1, "riis", N_SAYTEAM, cq->clientnum, text);
				}
				break;
			}

			case N_SWITCHNAME:
			{
				QUEUE_MSG;
				getstring(text, p);
				string newname;
				filtertext(newname, text, false, MAXNAMELEN);
				if(!newname[0]) copystring(ci->name, "unnamed");
				outf(2 | OUT_NOGAME, "%s is now known as %s\n", ci->name, newname);
				http_post_event_namechange(ci, newname);
				copystring(ci->name, newname);
				QUEUE_STR(ci->name);
				break;
			}

			case N_SWITCHMODEL:
			{
				ci->playermodel = getint(p);
				QUEUE_MSG;
				break;
			}

			case N_SWITCHTEAM:
			{
				getstring(text, p);
				filtertext(text, text, false, MAXTEAMLEN);
				if(strcmp(ci->team, text) && m_teammode && (!smode || smode->canchangeteam(ci, ci->team, text)))
				{
					if(ci->state.state==CS_ALIVE) suicide(ci);
					copystring(ci->team, text);
					aiman::changeteam(ci);
					sendf(-1, 1, "riisi", N_SETTEAM, sender, ci->team, ci->state.state==CS_SPECTATOR ? -1 : 0);
				}
				break;
			}

			case N_MAPVOTE:
			case N_MAPCHANGE:
			{
				getstring(text, p);
				filtertext(text, text, false);
				int reqmode = getint(p);
				if(type!=N_MAPVOTE && !mapreload) break;
				vote(text, reqmode, sender);
				break;
			}

			case N_ITEMLIST:
			{
				if((ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || !notgotitems || strcmp(ci->clientmap, smapname)) { while(getint(p)>=0 && !p.overread()) getint(p); break; }
				int n;
				while((n = getint(p))>=0 && n<MAXENTS && !p.overread())
				{
					server_entity se = { NOTUSED, 0, false };
					while(sents.length()<=n) sents.add(se);
					sents[n].type = getint(p);
					if(canspawnitem(sents[n].type))
					{
					    if(m_mp(gamemode) && delayspawn(sents[n].type)) sents[n].spawntime = spawntime(sents[n].type);
					    else sents[n].spawned = true;
					}
				}
				notgotitems = false;
				break;
			}

			case N_EDITENT:
			{
				int i = getint(p);
				loopk(3) getint(p);
				int type = getint(p);
				loopk(5) getint(p);
				if(!ci || ci->state.state==CS_SPECTATOR) break;
				QUEUE_MSG;
				bool canspawn = canspawnitem(type);
				if(i<MAXENTS && (sents.inrange(i) || canspawnitem(type)))
				{
					server_entity se = { NOTUSED, 0, false };
					while(sents.length()<=i) sents.add(se);
					sents[i].type = type;
					if(canspawn ? !sents[i].spawned : (sents[i].spawned || sents[i].spawntime))
					{
					    sents[i].spawntime = canspawn ? 1 : 0;
					    sents[i].spawned = false;
					}
				}
				break;
			}

			case N_EDITVAR:
			{
				int type = getint(p);
				getstring(text, p);
				switch(type)
				{
					case ID_VAR: getint(p); break;
					case ID_FVAR: getfloat(p); break;
					case ID_SVAR: getstring(text, p);
				}
				if(ci && ci->state.state!=CS_SPECTATOR) QUEUE_MSG;
				break;
			}

			case N_PING:
				sendf(sender, 1, "i2", N_PONG, getint(p));
				break;

			case N_CLIENTPING:
			{
				int ping = getint(p);
				if(ci)
				{
					ci->ping = ping;
					loopv(ci->bots) ci->bots[i]->ping = ping;
				}
				QUEUE_MSG;
				break;
			}

			case N_MASTERMODE:
			{
				int mm = getint(p);
				if((ci->privilege || ci->local) && mm>=MM_OPEN && mm<=MM_PRIVATE)
				{
					if((ci->privilege>=PRIV_ADMIN || ci->local) || (mastermask&(1<<mm)))
					{
						setmastermode(mm);
					}
					else
					{
					    defformatstring(s)("mastermode %d is disabled on this server", mm);
					    sendf(sender, 1, "ris", N_SERVMSG, s);
					}
				}
				break;
			}

			case N_CLEARBANS:
			{
				if(ci->privilege || ci->local)
				{
					clearbans();
				}
				break;
			}

			case N_KICK:
			{
				int victim = getint(p);
				if((ci->privilege || ci->local) && ci->clientnum!=victim) kick_client(victim, ci);
				break;
			}

			case N_SPECTATOR:
			{
				int spec = getint(p), val = getint(p);
				if(!ci->privilege && !ci->local && (spec!=sender || (ci->state.state==CS_SPECTATOR && mastermode>=MM_LOCKED))) break;
				spectator(val, spec);
				break;
			}

			case N_SETTEAM:
			{
				int who = getint(p);
				getstring(text, p);
				filtertext(text, text, false, MAXTEAMLEN);
				if(!ci->privilege && !ci->local) break;
				clientinfo *wi = getinfo(who);
				if(!wi || !strcmp(wi->team, text)) break;
				if(!smode || smode->canchangeteam(wi, wi->team, text))
				{
					if(wi->state.state==CS_ALIVE) suicide(wi);
					copystring(wi->team, text, MAXTEAMLEN+1);
				}
				aiman::changeteam(wi);
				sendf(-1, 1, "riisi", N_SETTEAM, who, wi->team, 1);
				break;
			}

			case N_FORCEINTERMISSION:
				if(ci->local && !hasnonlocalclients()) startintermission();
				break;

			case N_RECORDDEMO:
			{
				int val = getint(p);
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				demonextmatch = val!=0;
				defformatstring(msg)("demo recording is %s for next match", demonextmatch ? "enabled" : "disabled");
				outf(3, msg);
				break;
			}

			case N_STOPDEMO:
			{
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				stopdemo();
				break;
			}

			case N_CLEARDEMOS:
			{
				int demo = getint(p);
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				cleardemos(demo);
				break;
			}

			case N_LISTDEMOS:
				if(!ci->privilege && !ci->local && ci->state.state==CS_SPECTATOR) break;
				listdemos(sender);
				break;

			case N_GETDEMO:
			{
				int n = getint(p);
				if(!ci->privilege  && !ci->local && ci->state.state==CS_SPECTATOR) break;
				senddemo(sender, n);
				break;
			}

			case N_GETMAP:
				sendmap(ci);
				break;

			case N_NEWMAP:
			{
				int size = getint(p);
				if(!ci->privilege && !ci->local && ci->state.state==CS_SPECTATOR) break;
				if(size>=0)
				{
					smapname[0] = '\0';
					resetitems();
					notgotitems = false;
					if(smode) smode->reset(true);
					if(size < 10) size = 10;
					if(size > 16) size = 16;
					outf(2, "%s started a new map of size %d", ci->name, size);
					updateirctopic();
				}
				QUEUE_MSG;
				break;
			}

			case N_SETMASTER:
			{
				int val = getint(p);
				getstring(text, p);
				setmaster(ci, val!=0, text);
				// don't broadcast the master password
				break;
			}

			case N_ADDBOT:
			{
				aiman::reqadd(ci, getint(p));
				break;
			}

			case N_DELBOT:
			{
				aiman::reqdel(ci);
				break;
			}

			case N_BOTLIMIT:
			{
				int limit = getint(p);
				if(ci) aiman::setbotlimit(ci, limit);
				break;
			}

			case N_BOTBALANCE:
			{
				int balance = getint(p);
				if(ci) aiman::setbotbalance(ci, balance!=0);
				break;
			}

			case N_AUTHTRY:
			{
				string desc, name;
				getstring(desc, p, sizeof(desc)); // unused for now
				getstring(name, p, sizeof(name));
				if(!desc[0]) tryauth(ci, name);
				break;
			}

			case N_AUTHANS:
			{
				string desc, ans;
				getstring(desc, p, sizeof(desc)); // unused for now
				uint id = (uint)getint(p);
				getstring(ans, p, sizeof(ans));
				if(!desc[0]) answerchallenge(ci, id, ans);
				break;
			}

			case N_PAUSEGAME:
			{
				int val = getint(p);
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				pausegame(val > 0);
				break;
			}

			case N_COPY:
				ci->cleanclipboard();
				ci->lastclipboard = totalmillis;
				goto genericmsg;

			case N_PASTE:
				if(ci->state.state!=CS_SPECTATOR) sendclipboard(ci);
				goto genericmsg;

			case N_CLIPBOARD:
			{
				int unpacklen = getint(p), packlen = getint(p); 
				ci->cleanclipboard(false);
				if(ci->state.state==CS_SPECTATOR)
				{
					if(packlen > 0) p.subbuf(packlen);
					break;
				}
				if(packlen <= 0 || packlen > (1<<16) || unpacklen <= 0) 
				{
					if(packlen > 0) p.subbuf(packlen);
					packlen = unpacklen = 0;
				}
				packetbuf q(32 + packlen, ENET_PACKET_FLAG_RELIABLE);
				putint(q, N_CLIPBOARD);
				putint(q, ci->clientnum);
				putint(q, unpacklen);
				putint(q, packlen); 
				if(packlen > 0) p.get(q.subbuf(packlen).buf, packlen);
				ci->clipboard = q.finalize();
				ci->clipboard->referenceCount++;
				break;
			} 
					 
			#define PARSEMESSAGES 1
			#include "capture.h"
			#include "ctf.h"
			#undef PARSEMESSAGES

			case -1:
				disconnect_client(sender, DISC_TAGT);
				return;

			case -2:
				disconnect_client(sender, DISC_OVERFLOW);
				return;

			default: genericmsg:
			{
				int size = server::msgsizelookup(type);
				if(size<=0) { disconnect_client(sender, DISC_TAGT); return; }
				loopi(size-1) getint(p);
				if(ci && cq && (ci != cq || ci->state.state!=CS_SPECTATOR)) { QUEUE_AI; QUEUE_MSG; }
				break;
			}
		}
	}

	int laninfoport() { return SAUERBRATEN_LANINFO_PORT; }
	int serverinfoport(int servport) { return servport < 0 ? SAUERBRATEN_SERVINFO_PORT : servport+1; }
	int serverport(int infoport) { return infoport < 0 ? SAUERBRATEN_SERVER_PORT : infoport-1; }
	const char *defaultmaster() { return "sauerbraten.org"; }
	int masterport() { return SAUERBRATEN_MASTER_PORT; }
	int numchannels() { return 3; }

	#include "extinfo.h"

	void serverinforeply(ucharbuf &req, ucharbuf &p)
	{
		if(!getint(req))
		{
			extserverinforeply(req, p);
			return;
		}

		putint(p, numclients(-1, false, true));
		putint(p, 5);                   // number of attrs following
		putint(p, PROTOCOL_VERSION);    // generic attributes, passed back below
		putint(p, gamemode);
		putint(p, max((gamelimit - gamemillis)/1000, 0));
		putint(p, maxclients);
		putint(p, serverpass[0] ? MM_PASSWORD : (!m_mp(gamemode) ? MM_PRIVATE : (mastermode || mastermask&MM_AUTOAPPROVE ? mastermode : MM_AUTH)));
		sendstring(smapname, p);
		sendstring(serverdesc, p);
		sendserverinforeply(p);
	}

	bool servercompatible(char *name, char *sdec, char *map, int ping, const vector<int> &attr, int np)
	{
		return attr.length() && attr[0]==PROTOCOL_VERSION;
	}

	#include "aiman.h"
}

