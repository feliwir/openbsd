/* $OpenBSD: dgst.c,v 1.14 2019/07/29 10:06:55 inoguchi Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define BUFSIZE	1024*8

int
do_fp(BIO * out, unsigned char *buf, BIO * bp, int sep, int binout,
    EVP_PKEY * key, unsigned char *sigin, int siglen,
    const char *sig_name, const char *md_name,
    const char *file, BIO * bmd);

static struct {
	int argsused;
	int debug;
	int do_verify;
	char *hmac_key;
	char *keyfile;
	int keyform;
	const EVP_MD *m;
	char *mac_name;
	STACK_OF(OPENSSL_STRING) *macopts;
	const EVP_MD *md;
	int out_bin;
	char *outfile;
	char *passargin;
	int separator;
	char *sigfile;
	STACK_OF(OPENSSL_STRING) *sigopts;
	int want_pub;
} dgst_config;

static void
list_md_fn(const EVP_MD * m, const char *from, const char *to, void *arg)
{
	const char *mname;
	/* Skip aliases */
	if (!m)
		return;
	mname = OBJ_nid2ln(EVP_MD_type(m));
	/* Skip shortnames */
	if (strcmp(from, mname))
		return;
	/* Skip clones */
	if (EVP_MD_flags(m) & EVP_MD_FLAG_PKEY_DIGEST)
		return;
	if (strchr(mname, ' '))
		mname = EVP_MD_name(m);
	BIO_printf(arg, "-%-14s to use the %s message digest algorithm\n",
	    mname, mname);
}

int
dgst_main(int argc, char **argv)
{
	unsigned char *buf = NULL;
	int i, err = 1;
	BIO *in = NULL, *inp;
	BIO *bmd = NULL;
	BIO *out = NULL;
#define PROG_NAME_SIZE  39
	char pname[PROG_NAME_SIZE + 1];
	EVP_PKEY *sigkey = NULL;
	unsigned char *sigbuf = NULL;
	int siglen = 0;
	char *passin = NULL;

	if (single_execution) {
		if (pledge("stdio cpath wpath rpath tty", NULL) == -1) {
			perror("pledge");
			exit(1);
		}
	}

	if ((buf = malloc(BUFSIZE)) == NULL) {
		BIO_printf(bio_err, "out of memory\n");
		goto end;
	}

	memset(&dgst_config, 0, sizeof(dgst_config));
	dgst_config.keyform = FORMAT_PEM;
	dgst_config.out_bin = -1;

	/* first check the program name */
	program_name(argv[0], pname, sizeof pname);

	dgst_config.md = EVP_get_digestbyname(pname);

	argc--;
	argv++;
	while (argc > 0) {
		if ((*argv)[0] != '-')
			break;
		if (strcmp(*argv, "-c") == 0)
			dgst_config.separator = 1;
		else if (strcmp(*argv, "-r") == 0)
			dgst_config.separator = 2;
		else if (strcmp(*argv, "-out") == 0) {
			if (--argc < 1)
				break;
			dgst_config.outfile = *(++argv);
		} else if (strcmp(*argv, "-sign") == 0) {
			if (--argc < 1)
				break;
			dgst_config.keyfile = *(++argv);
		} else if (!strcmp(*argv, "-passin")) {
			if (--argc < 1)
				break;
			dgst_config.passargin = *++argv;
		} else if (strcmp(*argv, "-verify") == 0) {
			if (--argc < 1)
				break;
			dgst_config.keyfile = *(++argv);
			dgst_config.want_pub = 1;
			dgst_config.do_verify = 1;
		} else if (strcmp(*argv, "-prverify") == 0) {
			if (--argc < 1)
				break;
			dgst_config.keyfile = *(++argv);
			dgst_config.do_verify = 1;
		} else if (strcmp(*argv, "-signature") == 0) {
			if (--argc < 1)
				break;
			dgst_config.sigfile = *(++argv);
		} else if (strcmp(*argv, "-keyform") == 0) {
			if (--argc < 1)
				break;
			dgst_config.keyform = str2fmt(*(++argv));
		}
		else if (strcmp(*argv, "-hex") == 0)
			dgst_config.out_bin = 0;
		else if (strcmp(*argv, "-binary") == 0)
			dgst_config.out_bin = 1;
		else if (strcmp(*argv, "-d") == 0)
			dgst_config.debug = 1;
		else if (!strcmp(*argv, "-hmac")) {
			if (--argc < 1)
				break;
			dgst_config.hmac_key = *++argv;
		} else if (!strcmp(*argv, "-mac")) {
			if (--argc < 1)
				break;
			dgst_config.mac_name = *++argv;
		} else if (strcmp(*argv, "-sigopt") == 0) {
			if (--argc < 1)
				break;
			if (!dgst_config.sigopts)
				dgst_config.sigopts = sk_OPENSSL_STRING_new_null();
			if (!dgst_config.sigopts || !sk_OPENSSL_STRING_push(dgst_config.sigopts, *(++argv)))
				break;
		} else if (strcmp(*argv, "-macopt") == 0) {
			if (--argc < 1)
				break;
			if (!dgst_config.macopts)
				dgst_config.macopts = sk_OPENSSL_STRING_new_null();
			if (!dgst_config.macopts || !sk_OPENSSL_STRING_push(dgst_config.macopts, *(++argv)))
				break;
		} else if ((dgst_config.m = EVP_get_digestbyname(&((*argv)[1]))) != NULL)
			dgst_config.md = dgst_config.m;
		else
			break;
		argc--;
		argv++;
	}

	if (dgst_config.do_verify && !dgst_config.sigfile) {
		BIO_printf(bio_err, "No signature to verify: use the -signature option\n");
		goto end;
	}
	if ((argc > 0) && (argv[0][0] == '-')) {	/* bad option */
		BIO_printf(bio_err, "unknown option '%s'\n", *argv);
		BIO_printf(bio_err, "options are\n");
		BIO_printf(bio_err, "-c              to output the digest with separating colons\n");
		BIO_printf(bio_err, "-r              to output the digest in coreutils format\n");
		BIO_printf(bio_err, "-d              to output debug info\n");
		BIO_printf(bio_err, "-hex            output as hex dump\n");
		BIO_printf(bio_err, "-binary         output in binary form\n");
		BIO_printf(bio_err, "-sign   file    sign digest using private key in file\n");
		BIO_printf(bio_err, "-verify file    verify a signature using public key in file\n");
		BIO_printf(bio_err, "-prverify file  verify a signature using private key in file\n");
		BIO_printf(bio_err, "-keyform arg    key file format (PEM)\n");
		BIO_printf(bio_err, "-out filename   output to filename rather than stdout\n");
		BIO_printf(bio_err, "-signature file signature to verify\n");
		BIO_printf(bio_err, "-sigopt nm:v    signature parameter\n");
		BIO_printf(bio_err, "-hmac key       create hashed MAC with key\n");
		BIO_printf(bio_err, "-mac algorithm  create MAC (not neccessarily HMAC)\n");
		BIO_printf(bio_err, "-macopt nm:v    MAC algorithm parameters or key\n");

		EVP_MD_do_all_sorted(list_md_fn, bio_err);
		goto end;
	}

	in = BIO_new(BIO_s_file());
	bmd = BIO_new(BIO_f_md());
	if (in == NULL || bmd == NULL) {
		ERR_print_errors(bio_err);
		goto end;
	}

	if (dgst_config.debug) {
		BIO_set_callback(in, BIO_debug_callback);
		/* needed for windows 3.1 */
		BIO_set_callback_arg(in, (char *) bio_err);
	}
	if (!app_passwd(bio_err, dgst_config.passargin, NULL, &passin, NULL)) {
		BIO_printf(bio_err, "Error getting password\n");
		goto end;
	}
	if (dgst_config.out_bin == -1) {
		if (dgst_config.keyfile)
			dgst_config.out_bin = 1;
		else
			dgst_config.out_bin = 0;
	}

	if (dgst_config.outfile) {
		if (dgst_config.out_bin)
			out = BIO_new_file(dgst_config.outfile, "wb");
		else
			out = BIO_new_file(dgst_config.outfile, "w");
	} else {
		out = BIO_new_fp(stdout, BIO_NOCLOSE);
	}

	if (!out) {
		BIO_printf(bio_err, "Error opening output file %s\n",
		    dgst_config.outfile ? dgst_config.outfile : "(stdout)");
		ERR_print_errors(bio_err);
		goto end;
	}
	if ((!!dgst_config.mac_name + !!dgst_config.keyfile + !!dgst_config.hmac_key) > 1) {
		BIO_printf(bio_err, "MAC and Signing key cannot both be specified\n");
		goto end;
	}
	if (dgst_config.keyfile) {
		if (dgst_config.want_pub)
			sigkey = load_pubkey(bio_err, dgst_config.keyfile, dgst_config.keyform, 0, NULL,
			    "key file");
		else
			sigkey = load_key(bio_err, dgst_config.keyfile, dgst_config.keyform, 0, passin,
			    "key file");
		if (!sigkey) {
			/*
			 * load_[pub]key() has already printed an appropriate
			 * message
			 */
			goto end;
		}
	}
	if (dgst_config.mac_name) {
		EVP_PKEY_CTX *mac_ctx = NULL;
		int r = 0;
		if (!init_gen_str(bio_err, &mac_ctx, dgst_config.mac_name, 0))
			goto mac_end;
		if (dgst_config.macopts) {
			char *macopt;
			for (i = 0; i < sk_OPENSSL_STRING_num(dgst_config.macopts); i++) {
				macopt = sk_OPENSSL_STRING_value(dgst_config.macopts, i);
				if (pkey_ctrl_string(mac_ctx, macopt) <= 0) {
					BIO_printf(bio_err,
					    "MAC parameter error \"%s\"\n",
					    macopt);
					ERR_print_errors(bio_err);
					goto mac_end;
				}
			}
		}
		if (EVP_PKEY_keygen(mac_ctx, &sigkey) <= 0) {
			BIO_puts(bio_err, "Error generating key\n");
			ERR_print_errors(bio_err);
			goto mac_end;
		}
		r = 1;
mac_end:
		if (mac_ctx)
			EVP_PKEY_CTX_free(mac_ctx);
		if (r == 0)
			goto end;
	}
	if (dgst_config.hmac_key) {
		sigkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL,
		    (unsigned char *) dgst_config.hmac_key, -1);
		if (!sigkey)
			goto end;
	}
	if (sigkey) {
		EVP_MD_CTX *mctx = NULL;
		EVP_PKEY_CTX *pctx = NULL;
		int r;
		if (!BIO_get_md_ctx(bmd, &mctx)) {
			BIO_printf(bio_err, "Error getting context\n");
			ERR_print_errors(bio_err);
			goto end;
		}
		if (dgst_config.do_verify)
			r = EVP_DigestVerifyInit(mctx, &pctx, dgst_config.md, NULL, sigkey);
		else
			r = EVP_DigestSignInit(mctx, &pctx, dgst_config.md, NULL, sigkey);
		if (!r) {
			BIO_printf(bio_err, "Error setting context\n");
			ERR_print_errors(bio_err);
			goto end;
		}
		if (dgst_config.sigopts) {
			char *sigopt;
			for (i = 0; i < sk_OPENSSL_STRING_num(dgst_config.sigopts); i++) {
				sigopt = sk_OPENSSL_STRING_value(dgst_config.sigopts, i);
				if (pkey_ctrl_string(pctx, sigopt) <= 0) {
					BIO_printf(bio_err,
					    "parameter error \"%s\"\n",
					    sigopt);
					ERR_print_errors(bio_err);
					goto end;
				}
			}
		}
	}
	/* we use md as a filter, reading from 'in' */
	else {
		if (dgst_config.md == NULL)
			dgst_config.md = EVP_sha256();
		if (!BIO_set_md(bmd, dgst_config.md)) {
			BIO_printf(bio_err, "Error setting digest %s\n", pname);
			ERR_print_errors(bio_err);
			goto end;
		}
	}

	if (dgst_config.sigfile && sigkey) {
		BIO *sigbio;
		siglen = EVP_PKEY_size(sigkey);
		sigbuf = malloc(siglen);
		if (sigbuf == NULL) {
			BIO_printf(bio_err, "out of memory\n");
			ERR_print_errors(bio_err);
			goto end;
		}
		sigbio = BIO_new_file(dgst_config.sigfile, "rb");
		if (!sigbio) {
			BIO_printf(bio_err, "Error opening signature file %s\n",
			    dgst_config.sigfile);
			ERR_print_errors(bio_err);
			goto end;
		}
		siglen = BIO_read(sigbio, sigbuf, siglen);
		BIO_free(sigbio);
		if (siglen <= 0) {
			BIO_printf(bio_err, "Error reading signature file %s\n",
			    dgst_config.sigfile);
			ERR_print_errors(bio_err);
			goto end;
		}
	}
	inp = BIO_push(bmd, in);

	if (dgst_config.md == NULL) {
		EVP_MD_CTX *tctx;
		BIO_get_md_ctx(bmd, &tctx);
		dgst_config.md = EVP_MD_CTX_md(tctx);
	}
	if (argc == 0) {
		BIO_set_fp(in, stdin, BIO_NOCLOSE);
		err = do_fp(out, buf, inp, dgst_config.separator, dgst_config.out_bin, sigkey, sigbuf,
		    siglen, NULL, NULL, "stdin", bmd);
	} else {
		const char *md_name = NULL, *sig_name = NULL;
		if (!dgst_config.out_bin) {
			if (sigkey) {
				const EVP_PKEY_ASN1_METHOD *ameth;
				ameth = EVP_PKEY_get0_asn1(sigkey);
				if (ameth)
					EVP_PKEY_asn1_get0_info(NULL, NULL,
					    NULL, NULL, &sig_name, ameth);
			}
			md_name = EVP_MD_name(dgst_config.md);
		}
		err = 0;
		for (i = 0; i < argc; i++) {
			int r;
			if (BIO_read_filename(in, argv[i]) <= 0) {
				perror(argv[i]);
				err++;
				continue;
			} else {
				r = do_fp(out, buf, inp, dgst_config.separator, dgst_config.out_bin,
				    sigkey, sigbuf, siglen, sig_name, md_name,
				    argv[i], bmd);
			}
			if (r)
				err = r;
			(void) BIO_reset(bmd);
		}
	}

 end:
	freezero(buf, BUFSIZE);
	if (in != NULL)
		BIO_free(in);
	free(passin);
	BIO_free_all(out);
	EVP_PKEY_free(sigkey);
	if (dgst_config.sigopts)
		sk_OPENSSL_STRING_free(dgst_config.sigopts);
	if (dgst_config.macopts)
		sk_OPENSSL_STRING_free(dgst_config.macopts);
	free(sigbuf);
	if (bmd != NULL)
		BIO_free(bmd);

	return (err);
}

int
do_fp(BIO * out, unsigned char *buf, BIO * bp, int sep, int binout,
    EVP_PKEY * key, unsigned char *sigin, int siglen,
    const char *sig_name, const char *md_name,
    const char *file, BIO * bmd)
{
	size_t len;
	int i;

	for (;;) {
		i = BIO_read(bp, (char *) buf, BUFSIZE);
		if (i < 0) {
			BIO_printf(bio_err, "Read Error in %s\n", file);
			ERR_print_errors(bio_err);
			return 1;
		}
		if (i == 0)
			break;
	}
	if (sigin) {
		EVP_MD_CTX *ctx;
		BIO_get_md_ctx(bp, &ctx);
		i = EVP_DigestVerifyFinal(ctx, sigin, (unsigned int) siglen);
		if (i > 0)
			BIO_printf(out, "Verified OK\n");
		else if (i == 0) {
			BIO_printf(out, "Verification Failure\n");
			return 1;
		} else {
			BIO_printf(bio_err, "Error Verifying Data\n");
			ERR_print_errors(bio_err);
			return 1;
		}
		return 0;
	}
	if (key) {
		EVP_MD_CTX *ctx;
		BIO_get_md_ctx(bp, &ctx);
		len = BUFSIZE;
		if (!EVP_DigestSignFinal(ctx, buf, &len)) {
			BIO_printf(bio_err, "Error Signing Data\n");
			ERR_print_errors(bio_err);
			return 1;
		}
	} else {
		len = BIO_gets(bp, (char *) buf, BUFSIZE);
		if ((int) len < 0) {
			ERR_print_errors(bio_err);
			return 1;
		}
	}

	if (binout)
		BIO_write(out, buf, len);
	else if (sep == 2) {
		for (i = 0; i < (int) len; i++)
			BIO_printf(out, "%02x", buf[i]);
		BIO_printf(out, " *%s\n", file);
	} else {
		if (sig_name)
			BIO_printf(out, "%s-%s(%s)= ", sig_name, md_name, file);
		else if (md_name)
			BIO_printf(out, "%s(%s)= ", md_name, file);
		else
			BIO_printf(out, "(%s)= ", file);
		for (i = 0; i < (int) len; i++) {
			if (sep && (i != 0))
				BIO_printf(out, ":");
			BIO_printf(out, "%02x", buf[i]);
		}
		BIO_printf(out, "\n");
	}
	return 0;
}
