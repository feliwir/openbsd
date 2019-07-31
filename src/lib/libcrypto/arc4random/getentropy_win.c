/*	$OpenBSD: getentropy_win.c,v 1.5 2016/08/07 03:27:21 tb Exp $	*/

/*
 * Copyright (c) 2014, Theo de Raadt <deraadt@openbsd.org> 
 * Copyright (c) 2014, Bob Beck <beck@obtuse.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Emulation of getentropy(2) as documented at:
 * http://man.openbsd.org/getentropy.2
 */

#include <windows.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <bcrypt.h>
#include <process.h>

int	getentropy(void *buf, size_t len);

/*
 * On Windows, CryptGenRandom is supposed to be a well-seeded
 * cryptographically strong random number generator.
 */
int
getentropy(void *buf, size_t len)
{
	if (len > 256) {
		errno = EIO;
		return (-1);
	}

	if (FAILED(BCryptGenRandom(NULL, (BYTE*)buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
	{
		errno = EIO;
		return (-1);
	}

	return (0);
}
