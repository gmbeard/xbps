/*-
 * Copyright (c) 2008-2012 Juan Romero Pardines.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>

#include <xbps_api.h>
#include "defs.h"

static struct xbps_handle xh;

static void __attribute__((noreturn))
usage(bool fail)
{
	fprintf(stdout,
	    "Usage: xbps-install [OPTIONS] [PKGNAME...]\n\n"
	    "OPTIONS\n"
	    " -A --automatic           Set automatic installation mode\n"
	    " -C --config <file>       Full path to configuration file\n"
	    " -c --cachedir <dir>      Full path to cachedir\n"
	    " -d --debug               Debug mode shown to stderr\n"
	    " -f --force               Force package installation\n"
	    " -h --help                Print help usage\n"
	    " -n --dry-run             Dry-run mode\n"
	    " -p --print-format <fmt>  Print format for dry-run mode\n"
	    " -R --repository <uri>    Default repository to be used if config not set\n"
	    " -r --rootdir <dir>       Full path to rootdir\n"
	    " -s --skip-sync           Skip remote repository index sync\n"
	    " -u --update              Update target package(s)\n"
	    " -v --verbose             Verbose messages\n"
	    " -y --yes                 Assume yes to all questions\n"
	    " -V --version             Show XBPS version\n");
	exit(fail ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void __attribute__((noreturn))
cleanup_sighandler(int signum)
{
	xbps_end(&xh);
	_exit(signum);
}

int
main(int argc, char **argv)
{
	const char *shortopts = "AC:c:dfhnp:R:r:suVvy";
	const struct option longopts[] = {
		{ "automatic", no_argument, NULL, 'A' },
		{ "config", required_argument, NULL, 'C' },
		{ "cachedir", required_argument, NULL, 'c' },
		{ "debug", no_argument, NULL, 'd' },
		{ "force", no_argument, NULL, 'f' },
		{ "help", no_argument, NULL, 'h' },
		{ "dry-run", no_argument, NULL, 'n' },
		{ "print-format", required_argument, NULL, 'p' },
		{ "repository", required_argument, NULL, 'R' },
		{ "rootdir", required_argument, NULL, 'r' },
		{ "skip-sync", no_argument, NULL, 's' },
		{ "update", no_argument, NULL, 'u' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "version", no_argument, NULL, 'V' },
		{ "yes", no_argument, NULL, 'y' },
		{ NULL, 0, NULL, 0 }
	};
	struct xferstat xfer;
	struct sigaction sa;
	const char *rootdir, *cachedir, *conffile, *defrepo, *pformat;
	int i, c, flags, rv;
	bool skip_sync, yes, reinstall, drun, update;
	size_t maxcols;

	rootdir = cachedir = conffile = defrepo = pformat = NULL;
	flags = rv = 0;
	skip_sync = yes = reinstall = drun = update = false;

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (c) {
		case 'A':
			flags |= XBPS_FLAG_INSTALL_AUTO;
			break;
		case 'C':
			conffile = optarg;
			break;
		case 'c':
			cachedir = optarg;
			break;
		case 'd':
			flags |= XBPS_FLAG_DEBUG;
			break;
		case 'f':
			flags |= XBPS_FLAG_FORCE_INSTALL;
			reinstall = true;
			break;
		case 'h':
			usage(false);
			/* NOTREACHED */
		case 'n':
			drun = true;
			break;
		case 'p':
			pformat = optarg;
			break;
		case 'R':
			defrepo = optarg;
			break;
		case 'r':
			rootdir = optarg;
			break;
		case 's':
			skip_sync = true;
			break;
		case 'u':
			update = true;
			break;
		case 'v':
			flags |= XBPS_FLAG_VERBOSE;
			break;
		case 'V':
			printf("%s\n", XBPS_RELVER);
			exit(EXIT_SUCCESS);
		case 'y':
			yes = true;
			break;
		case '?':
		default:
			usage(true);
			/* NOTREACHED */
		}
	}
	if (!update && (argc == optind))
		usage(true);

	/*
	 * Initialize libxbps.
	 */
	memset(&xh, 0, sizeof(xh));
	xh.state_cb = state_cb;
	xh.fetch_cb = fetch_file_progress_cb;
	xh.fetch_cb_data = &xfer;
	xh.rootdir = rootdir;
	xh.cachedir = cachedir;
	xh.conffile = conffile;
	xh.flags = flags;
	xh.repository = defrepo;
	if (flags & XBPS_FLAG_VERBOSE)
		xh.unpack_cb = unpack_progress_cb_verbose;

	if ((rv = xbps_init(&xh)) != 0) {
		xbps_error_printf("Failed to initialize libxbps: %s\n",
		    strerror(rv));
		exit(EXIT_FAILURE);
	}

	/*
	 * Register a signal handler to clean up resources used by libxbps.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = cleanup_sighandler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	maxcols = get_maxcols();
	/*
	 * Check that we have write permission on rootdir, metadir
	 * and cachedir.
	 */
	if ((!drun && ((access(xh.rootdir, W_OK) == -1) ||
	    (access(xh.metadir, W_OK) == -1) ||
	    (access(xh.cachedir, W_OK) == -1)))) {
		if (errno != ENOENT) {
			fprintf(stderr, "Not enough permissions on "
			    "rootdir/cachedir/metadir: %s\n",
			    strerror(errno));
			rv = errno;
			goto out;
		}
	}

	/* Sync remote repository index files by default */
	if (!skip_sync || !drun) {
		rv = xbps_rpool_sync(&xh, XBPS_PKGINDEX, NULL);
		if (rv != 0)
			goto out;
	}

	if (update && (argc == optind)) {
		/* Update all installed packages */
		rv = dist_upgrade(&xh, maxcols, yes, drun);
	} else if (update) {
		/* Update target packages */
		for (i = optind; i < argc; i++) {
			rv = update_pkg(&xh, argv[i]);
			if (rv != 0)
				goto out;
		}
		rv = exec_transaction(&xh, maxcols, yes, drun);
	} else if (!update) {
		/* Install target packages */
		for (i = optind; i < argc; i++) {
			rv = install_new_pkg(&xh, argv[i], reinstall);
			if (rv != 0)
				goto out;
		}
		rv = exec_transaction(&xh, maxcols, yes, drun);
	}

out:
	xbps_end(&xh);
	exit(rv);
}
