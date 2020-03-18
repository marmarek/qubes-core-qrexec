/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2015  Marek Marczykowski-Górecki  <marmarek@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include "qrexec.h"
#include <libvchan.h>
#include "libqrexec-utils.h"
#include "qrexec-agent.h"

extern char **environ;
const bool qrexec_is_fork_server = true;

void do_exec(const char *cmd, const char *user __attribute__((unused)))
{
    char *shell;

    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    /* call QUBESRPC if requested */
    exec_qubes_rpc_if_requested(cmd, environ);

    /* otherwise, pass it to shell */
    shell = getenv("SHELL");
    if (!shell)
        shell = "/bin/sh";

    execl(shell, basename(shell), "-c", cmd, NULL);
    PERROR("execl");
    exit(1);
}

_Noreturn void handle_vchan_error(const char *op)
{
    PERROR("Error while vchan %s, exiting", op);
    exit(1);
}

static void handle_single_command(int fd, struct qrexec_cmd_info *info) {
    char cmdline[info->cmdline_len+1];

    if (!read_all(fd, cmdline, info->cmdline_len))
        return;
    cmdline[info->cmdline_len] = 0;

    handle_new_process(info->type, info->connect_domain,
            info->connect_port,
            cmdline, info->cmdline_len);
}

int main(int argc, char **argv) {
    int s, fd;
    char *socket_path;
    struct qrexec_cmd_info info;
    struct sockaddr_un peer;
    unsigned int addrlen;

    setup_logging("qrexec-fork-server");

    if (argc == 2) {
        socket_path = argv[1];
    } else if (argc == 1) {
        /* this will be leaked, but we don't care as the process will then terminate */
        if (asprintf(&socket_path, QREXEC_FORK_SERVER_SOCKET, getenv("USER")) < 0) {
            PERROR("Memory allocation failed");
            exit(1);
        }
    } else {
        fprintf(stderr, "Usage: %s [socket path]\n", argv[0]);
        exit(1);
    }

    s = get_server_socket(socket_path);
    if (fcntl(s, F_SETFD, O_CLOEXEC) < 0) {
        PERROR("fcntl");
        exit(1);
    }
    /* fork into background */
    switch (fork()) {
        case -1:
            PERROR("fork");
            exit(1);
        case 0:
            break;
        default:
            exit(0);
    }
    signal(SIGCHLD, SIG_IGN);
    register_exec_func(do_exec);

    while (1) {
        addrlen = sizeof(peer);
        fd = accept(s, (struct sockaddr *) &peer, &addrlen);
        if (fd < 0)
            break;
        if (read_all(fd, &info, sizeof(info))) {
            handle_single_command(fd, &info);
        }
        close(fd);
    }
    close(s);
    unlink(socket_path);
    return 0;
}
