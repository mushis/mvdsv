/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "qwsvdef.h"
#include "winquake.h"

#define MAX_ACCOUNTS 1000
#define MAX_FAILURES 10
#define MAX_LOGINNAME 16
#define ACC_FILE "accounts"
#define ACC_DIR "users"

cvar_t	sv_login = {"sv_login", "0"};	// if enabled, login required

typedef enum {a_free, a_ok, a_blocked} acc_state_t;
typedef enum {use_log, use_ip} use_t;

typedef struct
{
	char		login[MAX_LOGINNAME];
	char		pass[MAX_LOGINNAME];
	int			failures;
	int			inuse;
	ipfilter_t	adress;
	acc_state_t state;
	use_t		use; 
} account_t;

static account_t	accounts[MAX_ACCOUNTS];
static int			num_accounts = 0;

static qboolean validAcc(char *acc)
{
	char *s = acc;

	for (; *acc; acc++)
	{
		if (*acc < 'a' || *acc > 'z')
			if (*acc < 'A' || *acc > 'Z')
				if (*acc < '0' || *acc > '9')
					return false;
	}

	return acc - s <= MAX_LOGINNAME && acc - s >= 3 ;
}

/*
=================
WriteAccounts

Writes account list to disk
=================
*/

static void WriteAccounts()
{
	int i,c;
	FILE *f;
	account_t *acc;

	Sys_mkdir(ACC_DIR);
	if ( (f = fopen( ACC_DIR "/" ACC_FILE ,"wt")) == NULL)
	{
		Con_Printf("Warning: couldn't open for writing " ACC_FILE "\n");
		return;
	}

	for (acc = accounts, c = 0; c < num_accounts; acc++)
	{
		if (acc->state == a_free)
			continue;

		if (acc->use == use_log)
			fprintf(f, "%s %s %d %d\n", acc->login, acc->pass, acc->state, acc->failures);
		else 
			fprintf(f, "%s %d\n", acc->login, acc->state);

		c++;
	}

	fclose(f);
}

/*
=================
SV_LoadAccounts

loads account list from disk
=================
*/

void SV_LoadAccounts(void)
{
	int i,c;
	FILE *f;
	char tmp[128];
	account_t *acc = accounts;

	if ( (f = fopen( ACC_DIR "/" ACC_FILE ,"rt")) == NULL)
	{
		Con_Printf("couldn't open " ACC_FILE "\n");
		return;
	}

	while (!feof(f))
	{
		memset(acc, 0, sizeof(account_t));

		fscanf(f, "%s", acc->login);
		if (StringToFilter(acc->login, &acc->adress))
		{
			strcpy(acc->pass, acc->login);
			acc->use = use_ip;
			fscanf(f, "%d\n", &acc->state);
		} else
			fscanf(f, "%s %d %d\n", acc->pass, &acc->state, &acc->failures);

		if (acc->state != a_free) // lol?
			acc++;
	}

	num_accounts = acc - accounts;

	fclose(f);
}



/*
=================
SV_CreateAccount_f

acc_create <login> [<password>]
if password is not given, login will be used for password
login/pass has to be max 16 chars and at least 3, only regular chars are acceptable
=================
*/

void SV_CreateAccount_f(void)
{
	int i, spot, c;
	ipfilter_t adr;
	use_t use;

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: acc_create <login> [<password>]\n       acc_create <adress>\nmaximum %d characters for login/pass\n", MAX_LOGINNAME - 1);
		return;
	}

	if (num_accounts == MAX_ACCOUNTS)
	{
		Con_Printf("MAX_ACCOUNTS reached\n");
		return;
	}

	if (StringToFilter(Cmd_Argv(1), &adr))
		use = use_ip;
	else {
		use = use_log;

		// validate user login/pass
		if (!validAcc(Cmd_Argv(1)))
		{
			Con_Printf("Invalid login!\n");
			return;
		}

		if (Cmd_Argc() == 4 && !validAcc(Cmd_Argv(2)))
		{
			Con_Printf("Invalid pass!\n");
			return;
		}
	}

	// find free spot, check if login exist;
	for (i = 0, c = 0, spot = -1 ; c < num_accounts; i++)
	{
		if (accounts[i].state == a_free) {
			if (spot == -1)	spot = i;
			continue;
		}

		if (!stricmp(accounts[i].login, Cmd_Argv(1)))
			break;

		c++;
	}

	if (c < num_accounts)
	{
		Con_Printf("Login already in use\n");
		return;
	}

	if (spot == -1)
		spot = i;

	// create an account
	num_accounts++;
	strcpy(accounts[spot].login, Cmd_Argv(1));
	if (Cmd_Argc() == 3)
		strcpy(accounts[spot].pass, Cmd_Argv(2));
	else
		strcpy(accounts[spot].pass, Cmd_Argv(1));

	accounts[spot].state = a_ok;
	accounts[spot].use = use;

	Con_Printf("login %s created\n", Cmd_Argv(1));
	WriteAccounts();
}

/*
=================
SV_RemoveAccount_f

acc_remove <login>
removes the login
=================
*/

void SV_RemoveAccount_f(void)
{
	int i, c;

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: acc_remove <login>\n");
		return;
	}

	for (i = 0, c = 0; c < num_accounts; i++)
	{
		if (accounts[i].state == a_free)
			continue;

		if (!stricmp(accounts[i].login, Cmd_Argv(1)))
		{
			if (accounts[i].inuse)
				SV_Logout(&svs.clients[accounts[i].inuse -1]);

			accounts[i].state = a_free;
			num_accounts--;
			Con_Printf("login %s removed\n", accounts[i].login);
			WriteAccounts();
			return;
		}

		c++;
	}

	Con_Printf("account for %s not found\n", Cmd_Argv(1));
}

/*
=================
SV_ListAccount_f

shows the list of accounts
=================
*/

void SV_ListAccount_f (void)
{
	int i,c;

	if (!num_accounts)
	{
		Con_Printf("account list is empty\n");
		return;
	}

	Con_Printf("account list:\n");

	for (i = 0, c = 0; c < num_accounts; i++)
	{
		if (accounts[i].state != a_free)
		{
			Con_Printf("%.16s %s\n", accounts[i].login, accounts[i].state == a_ok ? "" : "blocked");
			c++;
		}
	}

	Con_Printf("%d login(s) found\n", num_accounts);
}

/*
=================
SV_blockAccount

blocks/unblocks an account
=================
*/

void SV_blockAccount(qboolean block)
{
	int i, c;

	for (i = 0, c = 0; c < num_accounts; i++)
	{
		if (accounts[i].state == a_free)
			continue;

		if (!stricmp(accounts[i].login, Cmd_Argv(1)))
		{
			if (block)
			{
				accounts[i].state = a_blocked;
				Con_Printf("account %s blocked\n", Cmd_Argv(1));
				return;
			}

			if (accounts[i].state != a_blocked)
				Con_Printf("account %s not blocked\n", Cmd_Argv(1));
			else {
				accounts[i].state = a_ok;
				accounts[i].failures = 0;
				Con_Printf("account %s unblocked\n", Cmd_Argv(1));
			}

			return;
		}
		c++;
	}

	Con_Printf("account %s not found\n", Cmd_Argv(1));
}

void SV_UnblockAccount_f(void)
{
	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: acc_unblock <login>\n");
		return;
	}

	SV_blockAccount(false);
	WriteAccounts();
}

void SV_BlockAccount_f(void)
{
	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: acc_block <login>\n");
		return;
	}

	SV_blockAccount(true);
	WriteAccounts();
}


/*
=================
checklogin

returns positive value if login/pass are valid
values <= 0 indicates a failure
=================
*/

int checklogin(char *log, char *pass, int num, int use)
{
	int i,c;

	for (i = 0, c = 0; c < num_accounts; i++)
	{
		if (accounts[i].state == a_free)
			continue;

		if (use == accounts[i].use &&
			/*use == use_log && accounts[i].use == use_log && */!stricmp(log, accounts[i].login))
		{
			if (accounts[i].inuse)
				return -1;

			if (accounts[i].state == a_blocked)
				return -2;

			if (!stricmp(pass, accounts[i].pass)) {
				accounts[i].failures = 0;
				accounts[i].inuse = num;
				return i+1;
			}

			if (++accounts[i].failures >= MAX_FAILURES)
			{
				Sys_Printf("account %s blocked after %d failed login attempts\n", accounts[i].login, accounts[i].failures);
				accounts[i].state = a_blocked;
			}

			WriteAccounts();
					
			return 0;
		}
		
		/*if (use == use_ip && accounts[i].use == use_ip && )
		{
			if (accounts[i].inuse)
				return -1;

			if (accounts[i].state == a_blocked)
				return -2;

			accounts[i].inuse = num;
			return i+1;
		}
		*/

		c++;
	}

	return 0;
}

void Login_Init (void)
{
	Cvar_RegisterVariable (&sv_login);

	Cmd_AddCommand ("acc_create",SV_CreateAccount_f);
	Cmd_AddCommand ("acc_remove",SV_RemoveAccount_f);
	Cmd_AddCommand ("acc_list",SV_ListAccount_f);
	Cmd_AddCommand ("acc_unblock",SV_UnblockAccount_f);
	Cmd_AddCommand ("acc_block",SV_BlockAccount_f);

	// load account list
	SV_LoadAccounts();
}

/*
===============
SV_Login

called on connect after cmd new is issued
===============
*/

qboolean SV_Login(client_t *cl)
{
	char *ip;

	// is sv_login is disabled, login is not necessery
	if (!sv_login.value) {
		SV_Logout(cl);
		cl->logged = -1;
		return true;
	}

	// if we're already logged return (probobly map change)
	if (cl->logged > 0)
		return true;

	// sv_login == 1 -> spectators don't login
	if (sv_login.value == 1 && cl->spectator)
	{
		SV_Logout(cl);
		cl->logged = -1;
		return true;
	}

	// check for account for ip
	ip = va("%d.%d.%d.%d", cl->realip.ip[0], cl->realip.ip[1], cl->realip.ip[2], cl->realip.ip[3]);
	if (cl->logged = checklogin(ip, ip, cl - svs.clients + 1, use_ip) > 0)
	{
		strcpy(cl->login, ip);
		return true;
	}

	// need to login before connecting
	cl->logged = false;
	cl->login[0] = 0;

	MSG_WriteByte (&cl->netchan.message, svc_print);
	MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
	MSG_WriteString (&cl->netchan.message, "Enter your login and password:\n");

	return false;
}

void SV_Logout(client_t *cl)
{
	if (cl->logged > 0) {
		accounts[cl->logged-1].inuse = false;
		cl->login[0] = 0;
		cl->logged = 0;
	}
}

void SV_ParseLogin(client_t *cl)
{
	char *log, *pass;

	if (Cmd_Argc() > 2) {
		log = Cmd_Argv(1);
		pass = Cmd_Argv(2);
	} else { // bah usually whole text in 'say' is put into ""
		log = pass = Cmd_Argv(1);
		while (*pass && *pass != ' ')
			pass++;

		if (*pass)
			*pass++ = 0;

		while (*pass == ' ')
			pass++;
	}

	// if login is parsed, we read just a password
	if (cl->login[0]) {
		pass = log;
		log = cl->login;
	} else {
		Q_strncpyz(cl->login, log, sizeof(cl->login));
	}

	if (!*pass)
	{
		Q_strncpyz(cl->login, log, sizeof(cl->login));
		MSG_WriteByte (&cl->netchan.message, svc_print);
		MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
		MSG_WriteString (&cl->netchan.message, va("Password for %s:\n", cl->login));

		return;
	}

	cl->logged = checklogin(log, pass, cl - svs.clients + 1, use_log);

	switch (cl->logged)
	{
	case -2:
		MSG_WriteByte (&cl->netchan.message, svc_print);
		MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
		MSG_WriteString (&cl->netchan.message, "Login blocked\n");
		cl->logged = 0;
		cl->login[0] = 0;
		break;
	case -1:
		MSG_WriteByte (&cl->netchan.message, svc_print);
		MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
		MSG_WriteString (&cl->netchan.message, "Login in use!\ntry again:\n");
		cl->logged = 0;
		cl->login[0] = 0;
		break;
	case 0:
		MSG_WriteByte (&cl->netchan.message, svc_print);
		MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
		MSG_WriteString (&cl->netchan.message, va("Acces denided\nPassword for %s:\n", cl->login));
		break;
	default:
		Sys_Printf("%s logged in as %s\n", cl->name, cl->login);
		MSG_WriteByte (&cl->netchan.message, svc_print);
		MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
		MSG_WriteString (&cl->netchan.message, va("Welcome %s\n", log));

		MSG_WriteByte (&cl->netchan.message, svc_stufftext);
		MSG_WriteString (&cl->netchan.message, "cmd new\n");
	}
}

void SV_LoginCheckTimeOut(client_t *cl)
{
	if (cl->connection_started && realtime - cl->connection_started > 60)
	{
		Sys_Printf("login time out for %s\n", cl->name);

		MSG_WriteByte (&cl->netchan.message, svc_print);
		MSG_WriteByte (&cl->netchan.message, PRINT_HIGH);
		MSG_WriteString (&cl->netchan.message, "waited too long, Bye!\n");
		SV_DropClient(cl);
	}
}