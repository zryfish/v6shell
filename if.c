/*
 * if.c - a port of the Sixth Edition (V6) Unix conditional command
 */
/*-
 * Copyright (c) 2004-2007
 *	Jeffrey Allen Neitzel <jneitzel (at) sdf1 (dot) org>.
 *	All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY JEFFREY ALLEN NEITZEL ``AS IS'', AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL JEFFREY ALLEN NEITZEL BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	@(#)$Id$
 */
/*
 *	Derived from:
 *		- Sixth Edition Unix	/usr/source/s1/if.c
 *		- Seventh Edition Unix	/usr/src/cmd/test.c
 */
/*-
 * Copyright (C) Caldera International Inc.  2001-2002.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code and documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed or owned by Caldera
 *      International, Inc.
 * 4. Neither the name of Caldera International, Inc. nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
 * INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	lint
#include "rcsid.h"
OSH_RCSID("$Id$");
#endif	/* !lint */

#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pexec.h"

#define	IF_ERR		124	/* default exit status for errors */
#define	FSIZE_GZ	1	/* for the `-s' primary           */

#define	EXIT(s)		((getpid() == ifpid) ? exit((s)) : _exit((s)))

static	int	ac;
static	int	ap;
static	char	**av;
static	uid_t	ifeuid;
static	pid_t	ifpid;

/*@noreturn@*/ static
	void	doex(bool);
static	bool	e1(void);
static	bool	e2(void);
static	bool	e3(void);
static	bool	equal(/*@null@*/ const char *, /*@null@*/ const char *);
/*@noreturn@*/ static
	void	error(int, /*@null@*/ const char *, const char *);
static	bool	expr(void);
static	bool	ifaccess(/*@null@*/ const char *, int);
static	bool	ifstat(/*@null@*/ const char *, mode_t);
/*@null@*/ static
	char	*nxtarg(bool);

/*
 * NAME
 *	if - conditional command
 *
 * SYNOPSIS
 *	if expr [command [arg ...]]
 *
 * DESCRIPTION
 *	See the if(1) manual page for full details.
 */
int
main(int argc, char **argv)
{
	bool re;	/* return value of expr() */

	ifpid = getpid();
	ifeuid = geteuid();

	/*
	 * Set-ID execution is not supported.
	 */
	if (ifeuid != getuid() || getegid() != getgid())
		error(IF_ERR, NULL, "Set-ID execution denied");

	if (argc > 1) {
		ac = argc;
		av = argv;
		ap = 1;
		re = expr();
		if (re && ap < ac)
			doex(false);
	} else
		re = 0;

	return re ? 0 : 1;
}

/*
 * Evaluate the expression.
 * Return true (1) or false (0).
 */
static bool
expr(void)
{
	bool re;

	re = e1();
	if (equal(nxtarg(true), "-o"))
		return re | expr();
	ap--;
	return re;
}

static bool
e1(void)
{
	bool re;

	re = e2();
	if (equal(nxtarg(true), "-a"))
		return re & e1();
	ap--;
	return re;
}

static bool
e2(void)
{

	if (equal(nxtarg(true), "!"))
		return !e3();
	ap--;
	return e3();
}

static bool
e3(void)
{
	bool re;
	pid_t cpid, tpid;
	int cstat, d;
	char *a, *b;

	if ((a = nxtarg(true)) == NULL)
		error(IF_ERR, av[ap - 2], "expression expected");

	/*
	 * Deal w/ parentheses for grouping.
	 */
	if (equal(a, "(")) {
		re = expr();
		if (!equal(nxtarg(true), ")"))
			error(IF_ERR, a, ") expected");
		return re;
	}

	/*
	 * Execute command within braces to obtain its exit status.
	 */
	if (equal(a, "{")) {
		if ((cpid = fork()) == -1)
			error(IF_ERR, NULL, "Cannot fork - try again");
		if (cpid == 0)
			/**** Child! ****/
			doex(true);
		else {
			/**** Parent! ****/
			tpid = wait(&cstat);
			while ((a = nxtarg(true)) != NULL && !equal(a, "}"))
				;	/* nothing */
			if (a == NULL)
				ap--;
			return (tpid == cpid && cstat == 0) ? true : false;
		}
	}

	/*
	 * file existence/permission tests
	 */
	if (equal(a, "-e"))
		return ifaccess(nxtarg(false), F_OK);
	if (equal(a, "-r"))
		return ifaccess(nxtarg(false), R_OK);
	if (equal(a, "-w"))
		return ifaccess(nxtarg(false), W_OK);
	if (equal(a, "-x"))
		return ifaccess(nxtarg(false), X_OK);

	/*
	 * file existence/type tests
	 */
	if (equal(a, "-d"))
		return ifstat(nxtarg(false), S_IFDIR);
	if (equal(a, "-f"))
		return ifstat(nxtarg(false), S_IFREG);
	if (equal(a, "-h"))
		return ifstat(nxtarg(false), S_IFLNK);
	if (equal(a, "-s"))
		return ifstat(nxtarg(false), FSIZE_GZ);
	if (equal(a, "-t")) {
		/* Does the descriptor refer to a terminal device? */
		b = nxtarg(true);
		if (b == NULL || *b == '\0')
			error(IF_ERR, a, "digit expected");
		if (*b >= '0' && *b <= '9' && *(b + 1) == '\0') {
			d = *b - '0';
			if (d >= 0 && d <= 9 && "0123456789"[d % 10] == *b)
				return isatty(d) != 0;
		}
		error(IF_ERR, b, "not a digit");
	}

	/*
	 * Compare the string arguments.
	 */
	if ((b = nxtarg(true)) == NULL)
		error(IF_ERR, a, "operator expected");
	if (equal(b, "="))
		return equal(a, nxtarg(false));
	if (equal(b, "!="))
		return !equal(a, nxtarg(false));
	error(IF_ERR, b, "unknown operator");
	/*NOTREACHED*/
	return false;
}

static void
doex(bool ischld)
{
	char **xap, **xav;

	if (ap < 2 || ap > ac)	/* should never be true */
		error(IF_ERR, NULL, "Invalid argv index");

	xav = xap = &av[ap];
	while (*xap != NULL) {
		if (ischld && equal(*xap, "}"))
			break;
		xap++;
	}
	if (ischld && xap - xav > 0 && !equal(*xap, "}"))
		error(IF_ERR, av[ap - 1], "} expected");
	*xap = NULL;
	if (xav[0] == NULL)
		error(IF_ERR, ischld ? av[ap - 1] : NULL, "command expected");

	/* Use a built-in exit since there is no external exit utility. */
	if (equal(xav[0], "exit")) {
		(void)lseek(STDIN_FILENO, (off_t)0, SEEK_END);
		EXIT(0);
	}

	(void)pexec(xav[0], xav);
	if (errno == ENOEXEC)
		error(125, xav[0], "No shell!");
	if (errno != ENOENT && errno != ENOTDIR)
		error(126, xav[0], "cannot execute");
	error(127, xav[0], "not found");
}

/*
 * Check access permission for file according to mode while
 * dealing w/ special cases for the superuser and `-x':
 *	- Always grant search access on directories.
 *	- If not a directory, require at least one execute bit.
 * Return true  (1) if access is granted.
 * Return false (0) if access is denied.
 */
static bool
ifaccess(const char *file, int mode)
{
	struct stat sb;
	bool ra;

	if (file == NULL || *file == '\0')
		return false;

	ra = access(file, mode) == 0;

	if (ra && mode == X_OK && ifeuid == 0) {
		if (stat(file, &sb) < 0)
			ra = false;
		else if (S_ISDIR(sb.st_mode))
			ra = true;
		else
			ra = (sb.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0;
	}

	return ra;
}

static bool
ifstat(const char *file, mode_t type)
{
	struct stat sb;
	bool rs;

	if (file == NULL || *file == '\0')
		return false;

	if (type == S_IFLNK) {
		if (lstat(file, &sb) < 0)
			rs = false;
		else
			rs = (sb.st_mode & S_IFMT) == type;
	} else if (stat(file, &sb) < 0)
		rs = false;
	else if (type == S_IFDIR || type == S_IFREG)
		rs = (sb.st_mode & S_IFMT) == type;
	else if (type == FSIZE_GZ)
		rs = sb.st_size > (off_t)0;
	else
		rs = false;

	return rs;
}

static char *
nxtarg(bool rn)
{
	char *nap;

	if (ap < 1 || ap > ac)	/* should never be true */
		error(IF_ERR, NULL, "Invalid argv index");
	if (ap == ac) {
		if (rn) {
			ap++;
			return NULL;
		}
		error(IF_ERR, av[ap - 1], "argument expected");
	}
	nap = av[ap];
	ap++;
	return nap;
}

static bool
equal(const char *a, const char *b)
{

	if (a == NULL || b == NULL || *a != *b)
		return false;
	return strcmp(a, b) == 0;
}

static void
error(int es, const char *msg1, const char *msg2)
{

	if (msg1 != NULL)
		(void)fprintf(stderr, "if: %s: %s\n", msg1, msg2);
	else
		(void)fprintf(stderr, "if: %s\n", msg2);
	EXIT(es);
}
