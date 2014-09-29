/*
 * Copyright (C) 2012 Sven-Ola, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
 
/*
 * Small user space daemon to get the Android DNS server setting and
 * maintain the resolver config on a regulary basis. Note, that this
 * daemon has to be started by some running android programs, in order
 * to have the ANDROID_PROPERTY_WORKSPACE environment variable and
 * the /dev/ashmem file descriptor (usually #9) that is inherited from
 * the calling process. Another note: cannot be started from a login 
 * shell, because "bash --login" closes all inherited file descriptors.
 */
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "sys/system_properties.h"

/* For futex syscall */
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>

/*
 * This syscall is required by Android's properties handling in system_properties.c
 */
int __futex_wait(volatile void *ftx, int val, const struct timespec *timeout)
{
	return syscall(SYS_futex, ftx, FUTEX_WAIT, val, (void *)timeout, NULL, NULL);
}

#define DNS1_PROP "net.dns1"
#define DNS2_PROP "net.dns2"

#define SBIN_RESOLV_CONF "/sbin/resolvconf"

static int debug = 0;

static int update_resolvconf(int add, const char* iface, const char* dns1, const char* dns2) {
	FILE* f = 0;
	char s[256];
	snprintf(s, sizeof(s), "%s %s %s", SBIN_RESOLV_CONF, add ? "-a" : "-d", iface);
	if (0 != (f = popen(s, "w"))) {
		if (debug) printf("piping to \"%s\":\n", s);
		if (dns1 && *dns1) {
			if (debug) printf("nameserver %s\n", dns1);
			fprintf(f, "nameserver %s\n", dns1);
		}
		if (dns2 && *dns2) {
			if (debug) printf("nameserver %s\n", dns2);
			fprintf(f, "nameserver %s\n", dns2);
		}
		if (0 <= fclose(f)) return 0;
		perror("pclose");
	}
	else {
		perror(s);
	}
	return -1;
}

static void daemonize(void)
{
	int i;
	pid_t pid, sid;

	/* already a daemon */
	if (getppid() == 1) return;

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0) {
		perror("cannot fork");
		exit(EXIT_FAILURE);
	}

	/* If we got a good PID, then we can exit the parent process. */
	if (pid > 0) exit(EXIT_SUCCESS);

	/* At this point we are executing as the child process */

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) exit(EXIT_FAILURE);

	/* Change the current working directory.  This prevents the current
	   directory from being locked; hence not being able to remove it. */
	if ((chdir("/")) < 0) exit(EXIT_FAILURE);

	/* Close all file descriptors, otherwise e.g. "adb shell" hangs during exit */
	for(i = 0; i < getdtablesize(); i++) {
		close(i);
	}
}

int __system_properties_init(void);

static void sig_handler(int signum)
{
	switch(signum) {
		case SIGINT:
		case SIGTERM:
			update_resolvconf(0 == 1, "android", 0, 0);
			exit(0);
		break;
	}
}

int main(int argc, char* argv[]) {
	int ret;
	debug = (1 < argc && 0 == strcmp("-d", argv[1]));
	if (0 == (ret = __system_properties_init())) {
		char dns1[PROP_VALUE_MAX];
		char dns2[PROP_VALUE_MAX];
		if (0 == __system_property_get(DNS1_PROP, dns1)) dns1[0] = 0;
		if (0 == __system_property_get(DNS2_PROP, dns2)) dns2[0] = 0;
		if (0 == (ret = update_resolvconf(0 == 0, "android", dns1, dns2))) {
			struct sigaction sa;
			if (!debug) daemonize();

			memset(&sa, sizeof(sa), 0);
			sa.sa_handler = sig_handler;
			sa.sa_flags = SA_RESETHAND;
			sigemptyset(&sa.sa_mask);

			if (-1 == sigaction(SIGINT, &sa, NULL) || -1 == sigaction(SIGTERM, &sa, NULL)) {
				perror("sigaction");
				exit(EXIT_FAILURE);
			}
			while(1) {
				char new_dns1[PROP_VALUE_MAX];
				char new_dns2[PROP_VALUE_MAX];
				sleep(2);
				if (0 == __system_property_get(DNS1_PROP, new_dns1)) new_dns1[0] = 0;
				if (0 == __system_property_get(DNS2_PROP, new_dns2)) new_dns2[0] = 0;
				if (0 != strcmp(dns1, new_dns1) || 0 != strcmp(dns2, new_dns2)) {
					strcpy(dns1, new_dns1);
					strcpy(dns2, new_dns2);
					update_resolvconf(0 == 0, "android", dns1, dns2);
				}
			}
		}
		else {
			perror("run "SBIN_RESOLV_CONF" failed");
		}
	}
	else {
		fprintf(stderr, "Cannot access system properties via ANDROID_PROPERTY_WORKSPACE environment setting.\n");
	}
	return ret;
}
