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
 *  safe-system.so : Making system(3) safe for MPI jobs everywhere.
 */

#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

extern char **environ;

typedef int (*system_f) (const char * cmd);

static void * libc_handle;
static system_f real_system;

static int client_fd = -1;
static int server_fd = -1;

static int write_n (int fd, const void *buf, size_t n)
{
    size_t nleft;
    ssize_t nwritten;
    unsigned const char *p;

    p = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write (fd, p, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return (-1);
        }
        nleft -= nwritten;
        p += nwritten;
    }
    return (n);
}

static int read_n (int fd, void *buf, size_t n)
{
    size_t nleft;
    ssize_t nread;
    unsigned char *p;

    p = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read (fd, p, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return (-1);
        }
        else if (nread == 0) {          /* EOF */
            break;
        }
        nleft -= nread;
        p += nread;
    }
    return (n - nleft);
}


static int create_socketpair (void)
{
    int pfds[2];

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, pfds) < 0) {
        fprintf (stderr, "systemsafe: socketpair failed: %s\n", strerror (errno));
        return (-1);
    }

    client_fd = pfds[0];
    server_fd = pfds[1];

    fcntl (client_fd, F_SETFD, FD_CLOEXEC);
    fcntl (server_fd, F_SETFD, FD_CLOEXEC);

    return (0);
}

static int read_string (int fd, char **bufp)
{
    int len = 0;
    int rc;

    *bufp = NULL;
    
    /*
     *  Read string length
     */
    if ((rc = read_n (fd, &len, sizeof (int))) < 0) {
        fprintf (stderr, "systemsafe: read_string: %s\n", strerror (errno));
        return (-1);
    }
    
    if (rc == 0) 
        return (0);

    if ((*bufp = malloc (len + 1)) == NULL) {
        fprintf (stderr, "systemsafe: read_string: malloc (%d): %s\n", 
                len, strerror (errno));
        return (-1);
    }

    if ((rc = read_n (fd, *bufp, len)) < 0) {
        fprintf (stderr, "systemsafe: read_string: %s\n", strerror (errno));
        return (-1);
    }

    if (rc == 0)
        return (0);

    (*bufp) [len] = '\0';

    return (len);
}

static int write_string (int fd, const char *str)
{
    int len = strlen (str);
    int rc;

    if (write_n (fd, &len, sizeof (int)) < 0) { 
        fprintf (stderr, "systemsafe: write: %s\n", strerror (errno));
        return (-1);
    }

    rc = write_n (fd, str, len);

    return (rc);
}

void free_env (char **env)
{
    int i = 0;
    while (env [i]) 
        free (env [i++]);
    free (env);
    return;
}

int read_env (int fd, char ***envp)
{
    int envc = 0;
    int i;

    if (read_n (fd, &envc, sizeof (int)) < 0) {
        fprintf (stderr, "systemsafe: read_env: %s\n", strerror (errno));
        return (-1);
    }

    if (!(*envp = malloc ((envc + 1) * sizeof (**envp)))) {
        fprintf (stderr, "systemsafe: read_env: malloc: %s\n", strerror (errno));
        return (-1);
    }

    for (i = 0; i < envc; i++) {
        char *entry;
        if (read_string (fd, &entry) < 0) {
            fprintf (stderr, "systemsafe: %s\n", strerror (errno));
            free_env (*envp);
            return (-1);
        }

        if (strncmp ("LD_PRELOAD=", entry, 10) == 0)
            entry [11] = '\0';

        (*envp)[i] = entry;
    }

    (*envp)[envc] = NULL;

    return (0);
}

static void handle_system_request (int fd)
{
    char *cmd, *path, **env, **oldenv;
    int rc;

    if ((rc = read_string (fd, &cmd)) < 0) {
        fprintf (stderr, "systemsafe: read cmd: %s\n", strerror (errno));
        exit (0);
    }

    if (rc == 0) /* EOF, time to exit */
        exit (0);

    if (read_string (fd, &path) < 0) {
        fprintf (stderr, "systemsafe: read path: %s\n", strerror (errno));
        exit (0);
    }

    if (read_env (fd, &env) < 0) {
        fprintf (stderr, "systemsafe: read env: %s\n", strerror (errno));
        exit (0);
    }

    if (chdir (path) < 0) 
        fprintf (stderr, "systemsafe: Failed to chdir to %s: %s\n", 
                path, strerror (errno));

    oldenv = environ;
    environ = env;

    rc = (*real_system) (cmd);

    write_n (fd, &rc, sizeof (int));

    environ = oldenv;
    free_env (env);
    free (cmd);
    free (path);

    return;
}

static void system_server (void)
{
    char c = 0;
    close (client_fd);
    write (server_fd, &c, 1);
    for (;;)
        handle_system_request (server_fd);
    return;
}

static int create_system_server (void)
{
    pid_t pid;
    char c;

    create_socketpair ();

    if ((pid = fork ()) < 0)
        return (-1);

    if (pid == 0) {
        system_server ();
        exit (0);
    }

    close (server_fd);

    /*
     *  Wait for system_server setup to complete
     */
    read (client_fd, &c, 1);

    return (0);
}

static int write_env (int fd)
{
    int i, envc = 0;

    while (environ[envc])
        envc++;

    write (fd, &envc, sizeof (int));

    for (i = 0; i < envc; i++) 
        write_string (fd, environ [i]);

    return (0);
}

int system (const char *cmd)
{
    int rc;
    char path [4096];

    if (cmd == NULL) {
        errno = EINVAL;
        return (-1);
    }

    write_string (client_fd, cmd);
    write_string (client_fd, getcwd (path, sizeof (path)));
    write_env (client_fd);

    if (read (client_fd, &rc, sizeof (int)) < 0) {
        fprintf (stderr, "system: failed to read status from server: %s\n",
                strerror (errno));
        return (-1);
    }

    return (rc);
}

void __attribute__ ((constructor)) fork_safe_init (void) 
{
    if ((libc_handle = dlopen ("libc.so.6", RTLD_LAZY)) == NULL) {
        exit (1);
    }

    if ((real_system = dlsym (libc_handle, "system")) == NULL)
        exit (2);

    create_system_server ();

    return;
}


/*
 * vi: ts=4 sw=4 expandtab
 */

