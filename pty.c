/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 * 
 *  This file is part of chaos-spankings, a set of spank plugins for SLURM.
 * 
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

/*
 *   Hack to run task 0 under a pty for a slurm job.
 */
#include <stdlib.h>
#include <pty.h>
#include <utmp.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <signal.h>


#include <slurm/spank.h>

SPANK_PLUGIN (pty, 1)

/*
 *  Globals:
 */
static int do_pty = 0;
static int master = -1;
static int listenfd = -1;
static pid_t pid;
static struct termios termdefaults;

static int pty_opt_process (int val, const char *optarg, int remote);

struct spank_option spank_options[] =
{
	{ "pty", NULL, 
          "Allocate a pty for rank 0. Must also specify -u."
          " (Use of --pty implies --output=0)", 
	  0, 0, (spank_opt_cb_f) pty_opt_process
	},
	SPANK_OPTIONS_TABLE_END
};


struct pty_winsz {
	unsigned rows;
	unsigned cols;
};

static void pty_winsz_pack (struct pty_winsz *w)
{
	w->rows = htonl (w->rows);
	w->cols = htonl (w->cols);
}

static void pty_winsz_unpack (struct pty_winsz *w)
{
	w->rows = ntohl (w->rows);
	w->cols = ntohl (w->cols);
}

static int pty_opt_process (int val, const char *optarg, int remote) 
{
	do_pty = 1;
	return (0);
}

void process_pty ()
{
	unsigned char buf [4096];
	int len;

	if ((len = read (master, buf, sizeof (buf))) < 0) {
		if (errno == EAGAIN)
			return;
		if (errno == EIO) /* Why do we get this sometimes */
			return;
		slurm_error ("read (pty master): %m\n");
		exit (1);
	}
	else if (len == 0) {
		close (STDOUT_FILENO);
		close (master);
		master = -1;
		return;
	}

	write (STDOUT_FILENO, buf, len);
}

void process_stdin ()
{
	unsigned char buf [4096];
	int len;

	if ((len = read (STDIN_FILENO, buf, sizeof (buf))) < 0) {
		slurm_error ("stdin read: %m\n");
		exit (1);
	}
	else if (len == 0) {
		close (STDOUT_FILENO);
		master = -1;
		return;
	}

	write (master, buf, len);
} 

void check_for_slave_exit ()
{
	int status = 0;

	if (waitpid (pid, &status, WNOHANG) <= 0)
		return;

	if (WIFEXITED (status)) 
		exit (status);
}

static int fd_set_nonblocking (int fd)
{
	int fval;

	assert (fd >= 0);

	if ((fval = fcntl (fd, F_GETFL, 0)) < 0)
		return (-1);
	if (fcntl (fd, F_SETFL, fval | O_NONBLOCK) < 0)
		return (-1);
	return (0);
}

static int get_winsize (spank_t sp, struct winsize *wsp)
{
	char val [64];

	memset (wsp, 0, sizeof (*wsp));

	if (spank_getenv (sp, "SLURM_PTY_WIN_ROW", val, 64) == ESPANK_SUCCESS) {
		spank_unsetenv (sp, "SLURM_PTY_WIN_ROW");
		wsp->ws_row = atoi (val);
	}

	if (spank_getenv (sp, "SLURM_PTY_WIN_COL", val, 64) == ESPANK_SUCCESS) {
		spank_unsetenv (sp, "SLURM_PTY_WIN_COL");
		wsp->ws_col = atoi (val);
	}

	if (!wsp->ws_row && !wsp->ws_col)
		return (0);
	return (1);
}

int pty_connect_back (spank_t sp)
{
	char ip [64], port [16];
	struct sockaddr_in addr;
	int s;

	int rc = spank_getenv (sp, "SLURM_LAUNCH_NODE_IPADDR", ip, 64);
	if (rc != ESPANK_SUCCESS) {
		slurm_error ("failed to read SLURM_NODE_IPADDR in env!");
		return (-1);
	}

	if (spank_getenv (sp, "SLURM_PTY_PORT", port, 16) != ESPANK_SUCCESS) {
		slurm_error ("failed to read SLURM_PTY_PORT in env!");
		return (-1);
	}

	addr.sin_family = AF_INET;
	inet_aton (ip, &addr.sin_addr);
	addr.sin_port = htons (atoi (port));


	if ((s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		slurm_error ("pty: socket: %m");
		return (-1);
	}


	if (connect (s, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		slurm_error ("pty: connect: %m");
		close (s);
		return (-1);
	}

	return (s);
}

static int write_pty_winsize (int fd, struct winsize *ws)
{
	int len;
	struct pty_winsz winsz;

	winsz.rows = ws->ws_row;
	winsz.cols = ws->ws_col;

	pty_winsz_pack (&winsz);

	if ((len = write (fd, &winsz, sizeof (winsz))) < 0) {
		slurm_error ("write_pty_winsz: %m");
		return (-1);
	}

	return (len);
}

static int read_pty_winsize (int fd, struct winsize *ws)
{
	struct pty_winsz winsz;
	int len;

	if ((len = read (fd, &winsz, sizeof (winsz))) < 0) {
		slurm_error ("read_pty_winsz: %m");
		return (-1);
	}

	if (len == 0) {
		slurm_error ("read_pty_winsz: Remote closed connection.");
		return (-1);
	}

	pty_winsz_unpack (&winsz);

	memset (ws, 0, sizeof (*ws));

	ws->ws_col = winsz.cols;
	ws->ws_row = winsz.rows;

	return (0);
}

static void process_winsz_event (int fd, int master)
{
	struct winsize ws;

	if (read_pty_winsize (fd, &ws) < 0)
		return;

	ioctl (master, TIOCSWINSZ, &ws);
	kill (0, SIGWINCH);
}

static int no_close_stdio (spank_t sp)
{
	char val [64];
	const char var[] = "SLURM_PTY_NO_CLOSE_STDIO";

	if (spank_getenv (sp, var, val, 64) == ESPANK_SUCCESS) 
		return (1);
	return 0;
}

static void close_stdio (void)
{
	int devnull;

	if ((devnull = open ("/dev/null", O_RDWR)) < 0) {
		slurm_error ("Failed to open /dev/null: %m");
	}
	else {
		dup2 (devnull, STDOUT_FILENO);
		dup2 (devnull, STDIN_FILENO);
		dup2 (devnull, STDERR_FILENO);
		close (devnull);
	}
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
	int taskid;
	int rfd;
	struct winsize ws;
	struct winsize *wsp = NULL;

	if (!do_pty)
		return (0);

	spank_get_item (sp, S_TASK_GLOBAL_ID, &taskid);

	if (taskid != 0) {
		if (!no_close_stdio (sp))
			close_stdio ();
		return (0);
	}

	if ((rfd = pty_connect_back (sp)) < 0) {
		slurm_error ("Failed to connect back to pty server");
	}

	if (get_winsize (sp, &ws)) 
		wsp = &ws;

	if ((pid = forkpty (&master, NULL, NULL, wsp)) < 0) {
		slurm_error ("Failed to allocate a pty for rank 0: %m\n");
		return (0);
	}
	else if (pid == 0) {
		/* Child. Continue with SLURM code */
		return (0);
	} 

	/* Parent: process data from client */

	while (1) {
		struct pollfd fds[3];
		int rc;
		int nfds = 2;

		fd_set_nonblocking (master);
		fd_set_nonblocking (STDIN_FILENO);

		fds[0].fd = master;
		fds[1].fd = STDIN_FILENO;
		fds[0].events = POLLIN | POLLERR;
		fds[1].events = POLLIN | POLLERR;

		if (rfd >= 0) {
			fd_set_nonblocking (rfd);
			fds[2].fd = rfd;
			fds[2].events = POLLIN | POLLERR;
			nfds++;
		}


		if ((rc = poll (fds, 3, -1)) < 0) {
			slurm_error ("poll: %m\n");
			exit (1);
		}

		if (fds[0].revents & POLLERR) {
			check_for_slave_exit ();
			continue;
		}

		if (fds[0].revents & POLLIN) 
			process_pty ();

		if (fds[1].revents & POLLIN)
			process_stdin ();

		if (fds[2].revents & POLLIN)
			process_winsz_event (rfd, master);

		check_for_slave_exit ();
	}

	return (0);
}

static void pty_restore (void)
{
	/* STDIN is probably closed by now */
	if (tcsetattr (STDOUT_FILENO, TCSANOW, &termdefaults) < 0)
		fprintf (stderr, "tcsetattr: %s\n", strerror (errno));
}

static int set_winsize (spank_t sp)
{
	struct winsize ws;
	char buf[64];
	ioctl (STDIN_FILENO, TIOCGWINSZ, &ws);

	snprintf (buf, sizeof (buf), "%d", ws.ws_row);
	setenv ("SLURM_PTY_WIN_ROW", buf, 1);

	snprintf (buf, sizeof (buf), "%d", ws.ws_col);
	setenv ("SLURM_PTY_WIN_COL", buf, 1);

	return (0);
}

static void sigset_sigwinch (sigset_t *pset)
{
	sigemptyset (pset);
	sigaddset (pset, SIGWINCH);
}

static int notify_winsize_change (int fd)
{
	struct winsize ws;
	ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws);
	write_pty_winsize (fd, &ws);
	return (0);
}

/*
 *  Detect when a window size change event occurs.
 */
static int winch = 0;
static void handle_sigwinch (int sig)
{
	winch = 1;
	signal (SIGWINCH, handle_sigwinch);
}

static void * pty_thread (void *arg)
{
	int fd;
	sigset_t set;

	sigset_sigwinch (&set);
	pthread_sigmask (SIG_UNBLOCK, &set, NULL);

	signal (SIGWINCH, handle_sigwinch);

	if ((fd = accept (listenfd, NULL, NULL)) < 0) {
		slurm_error ("pty: accept: %m");
		return NULL;
	}

	for (;;) {
		poll (NULL, 0, -1);
		if (winch && notify_winsize_change (fd) < 0)
			return NULL;
		winch = 0;
	}

	return (NULL);
}

static int bind_wild (int sockfd)
{
	socklen_t len;
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);        /* bind ephemeral port */

	if (bind (sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		slurm_error ("bind: %m\n");
		return (-1);
	}
	len = sizeof(sin);
	if (getsockname(sockfd, (struct sockaddr *) &sin, &len) < 0)
		return (-1);
	return ntohs(sin.sin_port);

}

static int do_listen (int *fd, short *port)
{
	int rc, val;

	if ((*fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return -1;

	val = 1;
	rc = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (rc > 0) {
		goto cleanup;
	}

	*port = bind_wild (*fd);

	if ((rc = listen(*fd, 16)) < 0) {
		slurm_error ("listen: %m");
		goto cleanup;
	}

	return (0);

cleanup:
	close (*fd);
	return (-1);
}

static void set_pty_env (short port)
{
	char buf [64];

	snprintf (buf, sizeof (buf), "%hu", port);
	setenv ("SLURM_PTY_PORT", buf, 1);
}

static int pty_thread_create (spank_t sp)
{
	short port;
	int err;
	pthread_attr_t attr;
	pthread_t tid;

	if (do_listen (&listenfd, &port) < 0) {
		slurm_error ("Unable to create pty listen port: %m");
		return (-1);
	}
	set_pty_env (port);

	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	err = pthread_create (&tid, &attr, &pty_thread, NULL);
	pthread_attr_destroy (&attr);
	if (err)
		return (-1);
	return (0);
}


static void block_sigwinch (void)
{
	sigset_t set;
	sigset_sigwinch (&set);
	pthread_sigmask (SIG_BLOCK, &set, NULL);
}

int slurm_spank_local_user_init (spank_t sp, int ac, char **av)
{
	struct termios term;
	int fd = STDIN_FILENO;

	if (!do_pty)
		return (0);


	/* Save terminal settings for restore */
	tcgetattr (fd, &termdefaults); 
	tcgetattr (fd, &term);
	/* Set raw mode on local tty */
	cfmakeraw (&term);
	tcsetattr (fd, TCSANOW, &term);
	atexit (&pty_restore);

	set_winsize (sp);

	block_sigwinch ();

	pty_thread_create (sp);

	return (0);
}
