/*
 * Parallel XZ 4.999.9beta,
 * runs LZMA compression simultaneously on multiple cores.
 *
 * Copyright (C) 2009-2014 Jindrich Novy (jnovy@users.sourceforge.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define __STDC_FORMAT_MACROS

#include <string.h>
#include <stdio.h>
#include <err.h>
#if (!defined(__APPLE__) && !defined(__FreeBSD__))
#include <stdio_ext.h>
#endif
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#if (!defined(__APPLE__) && !defined(__FreeBSD__))
#include <error.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <utime.h>
#include <signal.h>
#include <getopt.h>
#include <fcntl.h>
#include <lzma.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef XZ_BINARY
#define XZ_BINARY "xz"
#endif

#define BUFFSIZE 0x10000

#define ADD_OPT(c) \
do { \
	size_t __s = strlen(xzcmd); \
	if (__s+3 >= xzcmd_max) { \
		fprintf(stderr,"xz command line too long\n"); \
		exit(EXIT_FAILURE); \
	} \
	xzcmd[__s] = ' '; \
	xzcmd[__s+1] = '-'; \
	xzcmd[__s+2] = c; \
	xzcmd[__s+3] = '\0';\
} while (0);

#if defined(__FreeBSD__) || defined(__APPLE__)
static size_t __fpending (FILE *fp)
{
    return (fp->_p - fp->_bf._base);
}
#endif


FILE **ftemp;
char str[0x100];
char buf[BUFFSIZE];
char *xzcmd;
size_t xzcmd_max;

unsigned opt_complevel = 6, opt_stdout, opt_keep, opt_threads, opt_verbose;
unsigned opt_force, opt_stdout;
double opt_context_size = 3;
FILE *fi, *fo;
char **file;
int files;

const char short_opts[] = "cC:defF:hHlkM:qQrS:tT:D:vVz0123456789";

const struct option long_opts[] = {
	// Operation mode
	{ "compress",       no_argument,       NULL,  'z' },
	{ "decompress",     no_argument,       NULL,  'd' },
	{ "uncompress",     no_argument,       NULL,  'd' },
	{ "test",           no_argument,       NULL,  't' },
	{ "list",           no_argument,       NULL,  'l' },
	// Operation modifiers
	{ "keep",           no_argument,       NULL,  'k' },
	{ "force",          no_argument,       NULL,  'f' },
	{ "stdout",         no_argument,       NULL,  'c' },
	{ "to-stdout",      no_argument,       NULL,  'c' },
	{ "suffix",         required_argument, NULL,  'S' },
//	{ "files",          optional_argument, NULL,  OPT_FILES },
//	{ "files0",         optional_argument, NULL,  OPT_FILES0 },
	// Basic compression settings
	{ "format",         required_argument, NULL,  'F' },
	{ "check",          required_argument, NULL,  'C' },
	{ "memory",         required_argument, NULL,  'M' },
	{ "threads",        required_argument, NULL,  'T' },
	{ "context-size",   required_argument, NULL,  'D' },
	{ "extreme",        no_argument,       NULL,  'e' },
	{ "fast",           no_argument,       NULL,  '0' },
	{ "best",           no_argument,       NULL,  '9' },
	// Filters
/*	{ "lzma1",          optional_argument, NULL,  OPT_LZMA1 },
	{ "lzma2",          optional_argument, NULL,  OPT_LZMA2 },
	{ "x86",            optional_argument, NULL,  OPT_X86 },
	{ "powerpc",        optional_argument, NULL,  OPT_POWERPC },
	{ "ia64",           optional_argument, NULL,  OPT_IA64 },
	{ "arm",            optional_argument, NULL,  OPT_ARM },
	{ "armthumb",       optional_argument, NULL,  OPT_ARMTHUMB },
	{ "sparc",          optional_argument, NULL,  OPT_SPARC },
	{ "delta",          optional_argument, NULL,  OPT_DELTA },
	{ "subblock",       optional_argument, NULL,  OPT_SUBBLOCK },
*/	// Other options
	{ "quiet",          no_argument,       NULL,  'q' },
	{ "verbose",        no_argument,       NULL,  'v' },
	{ "no-warn",        no_argument,       NULL,  'Q' },
	{ "help",           no_argument,       NULL,  'h' },
	{ "long-help",      no_argument,       NULL,  'H' },
	{ "version",        no_argument,       NULL,  'V' },
	{ NULL,             0,                 NULL,   0 }
};

void __attribute__((noreturn)) run_xz( char **argv ) {
	execvp(XZ_BINARY, argv);
	err(EXIT_FAILURE, "execution of "XZ_BINARY" binary failed");
}

void parse_args( int argc, char **argv ) {
	int c;
	
	opterr = 0;
	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
		switch (c) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				opt_complevel = c - '0';
				ADD_OPT(c);
				break;
			case 'v':
				opt_verbose = 1;
				break;
			case 'f':
				opt_force = 1;
				break;
			case 'e':
			case 'q':
			case 'Q':
				ADD_OPT(c);
				break;
			case 'c':
				opt_stdout = 1;
				break;
			case 'k':
				opt_keep = 1;
				break;
			case 'T':
				opt_threads = atoi(optarg);
				break;
			case 'D':
				opt_context_size = atof(optarg);
				if ( opt_context_size <= 0 ) {
					errx(EXIT_FAILURE, "Invalid context size specified");
				}
				break;
			case 'h':
			case 'H':
				printf("Parallel PXZ-"PXZ_VERSION"-"PXZ_BUILD_DATE", by Jindrich Novy <jnovy@users.sourceforge.net>\n\n"
					"Options:\n"
					"  -T, --threads       maximum number of threads to run simultaneously\n"
					"  -D, --context-size  per-thread compression context size as a multiple\n"
					"                      of dictionary size. Default is 3.\n\n"
					"Usage and other options are same as in XZ:\n\n");
				run_xz(argv);
				break;
			case 'V':
				printf("Parallel PXZ "PXZ_VERSION" (build "PXZ_BUILD_DATE")\n");
				run_xz(argv);
				break;
			case 'd':
			case 't':
			case 'l':
			case '?':
				run_xz(argv);
			default:
				break;
		}
	}
	
	if (!argv[optind]) {
		file = malloc(sizeof(*file));
		*file = "-";
		files = 1;
	} else {
		file = &argv[optind];
		files = argc-optind;
		for (c=0; c<files; c++) {
			struct stat s;
			
			if ( file[c][0] == '-' && file[c][1] == '\0' ) {
				continue;
			}
			
			if ( stat(file[c], &s)) {
				err(EXIT_FAILURE, "can't stat '%s'", file[c]);
			}
		}
	}
}

void __attribute__((noreturn) )term_handler( int sig __attribute__ ((unused)) ) {
	if ( fo != stdout && unlink(str) ) {
		err(EXIT_FAILURE, "error deleting corrupted target archive %s", str);
	}
	exit(EXIT_FAILURE);
}

int close_stream( FILE *f ) {
	if ( ferror(f) ) {
		if ( !fclose(f) ) {
			errno = 0;
		}
		return EOF;
	}
	
	if ( fclose(f) && (__fpending(f) || errno != EBADF) ) {
		return EOF;
	}
	
	return 0;
}

int main( int argc, char **argv ) {
	int i;
	uint64_t p, threads, chunk_size;
	uint8_t *m;
	struct stat s;
	ssize_t rd, ts = 0;
	size_t page_size;
	struct sigaction new_action, old_action;
	struct utimbuf u;
	lzma_filter filters[LZMA_FILTERS_MAX + 1];
	lzma_options_lzma lzma_options;
	
	xzcmd_max = sysconf(_SC_ARG_MAX);
	page_size = sysconf(_SC_PAGE_SIZE);
	xzcmd = malloc(xzcmd_max);
	snprintf(xzcmd, xzcmd_max, XZ_BINARY);
	
	parse_args(argc, argv);

	lzma_lzma_preset(&lzma_options, opt_complevel);

	filters[0].id = LZMA_FILTER_LZMA2;
	filters[0].options = &lzma_options;
	filters[1].id = LZMA_VLI_UNKNOWN;

	
	for (i=0; i<files; i++) {
		int std_in = file[i][0] == '-' && file[i][1] == '\0';
#ifdef _OPENMP
		threads = omp_get_max_threads();
#else
		threads = 1;
#endif
		if ( (rd=strlen(file[i])) >= 3 && !strncmp(&file[i][rd-3], ".xz", 3) ) {
			if (opt_verbose) {
				errx(EXIT_FAILURE, "ignoring '%s', it seems to be already compressed", file[i]);
			}
			continue;
		}
		
		if ( !std_in ) {
			if ( stat(file[i], &s)) {
				err(EXIT_FAILURE, "can't stat '%s'", file[i]);
			}
		}
		
		chunk_size = opt_context_size * lzma_options.dict_size;
		chunk_size = (chunk_size + page_size)&~(page_size-1);
		
		if ( opt_verbose ) {
			fprintf(stderr, "context size per thread: %"PRIu64" B\n", chunk_size);
		}
		
		if ( opt_threads && (threads > opt_threads || opt_force) ) {
			threads = opt_threads;
		}
		
		fo = stdout;
		if ( std_in ) {
			fi = stdin;
		} else {
			if ( !(fi=fopen(file[i], "rb")) ) {
				err(EXIT_FAILURE, "can't open '%s' for reading", file[i]);
			}
			if ( !opt_stdout ) {
				snprintf(str, sizeof(str), "%s.xz", file[i]);
				if ( !(fo=fopen(str, "wb")) ) {
					err(EXIT_FAILURE, "error creating target archive '%s'", str);
				}
			}
		}
		
		if ( opt_verbose ) {
			if ( fo != stdout ) {
				fprintf(stderr, "%s -> %"PRIu64"/%"PRIu64" thread%c: [", file[i], threads, (s.st_size+chunk_size-1)/chunk_size, threads != 1 ? 's' : ' ');
			} else {
				fprintf(stderr, "%"PRIu64" thread%c: [", threads, threads != 1 ? 's' : ' ');
			}
			fflush(stderr);
		}
		
		m  = malloc(threads*chunk_size);
		
		new_action.sa_handler = term_handler;
		sigemptyset (&new_action.sa_mask);
		new_action.sa_flags = 0;
		
		sigaction(SIGINT, NULL, &old_action);
		if (old_action.sa_handler != SIG_IGN) sigaction(SIGINT, &new_action, NULL);
		sigaction(SIGHUP, NULL, &old_action);
		if (old_action.sa_handler != SIG_IGN) sigaction(SIGHUP, &new_action, NULL);
		sigaction(SIGTERM, NULL, &old_action);
		if (old_action.sa_handler != SIG_IGN) sigaction(SIGTERM, &new_action, NULL);
		
		ftemp = malloc(threads*sizeof(ftemp[0]));
		
		while ( !feof(fi) ) {
			size_t actrd;
			
			for (p=0; p<threads; p++) {
				ftemp[p] = tmpfile();
			}
			
			for ( actrd=rd=0; !feof(fi) && !ferror(fi) && (uint64_t)rd < threads*chunk_size; rd += actrd) {
				actrd = fread(&m[rd], 1, threads*chunk_size-actrd, fi);
			}
			if (ferror(fi)) {
				err(EXIT_FAILURE, "error in reading input");
			}

#pragma omp parallel for private(p) num_threads(threads)
			for ( p=0; p<(rd+chunk_size-1)/chunk_size; p++ ) {
				off_t pt, len = rd-p*chunk_size >= chunk_size ? chunk_size : rd-p*chunk_size;
				uint8_t *mo;
				lzma_stream strm = LZMA_STREAM_INIT;
				lzma_ret ret;
				
				mo = malloc(BUFFSIZE);
				
				if ( lzma_stream_encoder(&strm, filters, LZMA_CHECK_CRC64) != LZMA_OK ) {
					err(EXIT_FAILURE, "unable to initialize LZMA encoder");
				}
				
				for (pt=0; pt<len; pt+=BUFFSIZE) {
					strm.next_in = &m[p*chunk_size+pt];
					strm.avail_in = len-pt >= BUFFSIZE ? BUFFSIZE : len-pt;
					strm.next_out = mo;
					strm.avail_out = BUFFSIZE;
					do {
						ret = lzma_code(&strm, LZMA_RUN);
						if ( ret != LZMA_OK ) {
							errx(EXIT_FAILURE, "error in LZMA_RUN");
						}
						if ( BUFFSIZE - strm.avail_out > 0 ) {
							if ( !fwrite(mo, 1, BUFFSIZE - strm.avail_out, ftemp[p]) ) {
								err(EXIT_FAILURE, "writing to temp file failed");
							}
							strm.next_out = mo;
							strm.avail_out = BUFFSIZE;
						}
					} while ( strm.avail_in );
				}
				
				strm.next_out = mo;
				strm.avail_out = BUFFSIZE;
				do {
					ret = lzma_code(&strm, LZMA_FINISH);
					if ( ret != LZMA_OK && ret != LZMA_STREAM_END ) {
						errx(EXIT_FAILURE, "error in LZMA_FINISH");
					}
					if ( BUFFSIZE - strm.avail_out > 0 ) {
						if ( !fwrite(mo, 1, BUFFSIZE - strm.avail_out, ftemp[p]) ) {
							err(EXIT_FAILURE, "writing to temp file failed");
						}
						strm.next_out = mo;
						strm.avail_out = BUFFSIZE;
					}
				} while ( ret == LZMA_OK );
				lzma_end(&strm);
				
				free(mo);
				
				if ( opt_verbose ) {
					fprintf(stderr, "%"PRIu64" ", p);
					fflush(stderr);
				}
			}
			
			for ( p=0; p<threads; p++ ) {
				rewind(ftemp[p]);
				while ( (rd=fread(buf, 1, sizeof(buf), ftemp[p])) > 0 ) {
					if ( fwrite(buf, 1, rd, fo) != (size_t)rd ) {
						warn("writing to archive failed");
						if ( fo != stdout && unlink(str) ) {
							warn("error deleting corrupted target archive %s", str);
						}
						exit(EXIT_FAILURE);
					} else ts += rd;
				}
				if (rd < 0) {
					warn("reading from temporary file failed");
					if ( fo != stdout && unlink(str) ) {
						warn("error deleting corrupted target archive %s", str);
					}
					exit(EXIT_FAILURE);
				}
				if ( close_stream(ftemp[p]) ) {
					warn("I/O error in temp file");
				}
			}
		}
		
		if ( fi != stdin && close_stream(fi) ) {
			warn("I/O error in input file");
		}
		
		if ( opt_verbose ) {
			fprintf(stderr, "] ");
		}

		free(ftemp);
		
		if ( fo != stdout ) {
			if ( close_stream(fo) ) {
				warn("I/O error in target archive");
			}
		} else return 0;
		
		if ( chmod(str, s.st_mode) ) {
			warn("warning: unable to change archive permissions");
		}

		u.actime = s.st_atime;
		u.modtime = s.st_mtime;
		
		if ( utime(str, &u) ) {
			warn("warning: unable to change archive timestamp");
		}
		
		sigaction(SIGINT, &old_action, NULL);
		sigaction(SIGHUP, &old_action, NULL);
		sigaction(SIGTERM, &old_action, NULL);
		
		if ( opt_verbose ) {
			fprintf(stderr, "%"PRIu64" -> %zd %3.3f%%\n", s.st_size, ts, ts*100./s.st_size);
		}
		
		if ( !opt_keep && unlink(file[i]) ) {
			warn("error deleting input file %s", file[i]);
		}
	}
	
	return 0;
}
