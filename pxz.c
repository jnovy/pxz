/*
 * Parallel XZ 4.999.9beta,
 * runs LZMA compression simultaneously on multiple cores.
 *
 * Copyright (C) 2009 Jindrich Novy (jnovy@users.sourceforge.net)
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <utime.h>
#include <signal.h>
#include <getopt.h>
#include <omp.h>
#include <lzma.h>

#ifndef XZ_BINARY
#define XZ_BINARY "xz"
#endif
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

FILE **ftemp;
char str[0x100];
char buf[0x10000];
char *xzcmd;
size_t xzcmd_max;

unsigned opt_complevel = 6, opt_stdout, opt_keep, opt_threads, opt_verbose;
unsigned opt_force, opt_stdout;
char **file;
int files;

const char short_opts[] = "cC:defF:hHlkM:qQrS:tT:vVz0123456789";

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
	perror("xz execution failed");
	exit(EXIT_FAILURE);
}

void parse_args( int argc, char **argv ) {
	int c;
	
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
			case 'h':
			case 'H':
				printf("Parallel PXZ-"PXZ_VERSION"-"PXZ_BUILD_DATE", by Jindrich Novy <jnovy@users.sourceforge.net>\n\n"
					"Options:\n"
					"  -T, --threads       specifies maximum threads to run simultaneously\n\n"
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
				run_xz(argv);
			default:
				break;
		}
	}
	
	if (!argv[optind]) {
		fprintf(stderr,"no files given.\n");
		exit(EXIT_FAILURE);
	} else {
		file = &argv[optind];
		files = argc-optind;
		for (c=0; c<files; c++) {
			struct stat s;
			
			if ( stat(file[c], &s)) {
				fprintf(stderr, "can't stat '%s'.\n", file[c]);
				exit(EXIT_FAILURE);
			}
		}
	}
}

void term_handler( int sig __attribute__ ((unused)) ) {
	unlink(str);
}

int main( int argc, char **argv ) {
	int i;
	uint64_t p, threads;
	struct stat s;
	uint8_t *m;
	FILE *f;
	ssize_t rd, ts = 0;
	struct sigaction new_action, old_action;
	struct utimbuf u;
	
	xzcmd_max = sysconf(_SC_ARG_MAX);
	xzcmd = malloc(xzcmd_max);
	snprintf(xzcmd, xzcmd_max, XZ_BINARY);
	
	parse_args(argc, argv);
	
	for (i=0; i<files; i++) {
#ifdef _OPENMP
		threads = omp_get_max_threads();
#else
		threads = 1;
#endif
		if ( (rd=strlen(file[i])) >= 3 && !strncmp(&file[i][rd-3], ".xz", 3) ) {
			if (opt_verbose) {
				fprintf(stderr, "ignoring '%s', it seems to be already compressed\n", file[i]);
			}
			continue;
		}
		if ( stat(file[i], &s)) {
			fprintf(stderr, "can't stat '%s'.\n", file[i]);
			exit(EXIT_FAILURE);
		}
		
		if ( ((uint64_t)s.st_size)>>(opt_complevel <= 1 ? 16 : opt_complevel + 17) < threads ) {
			threads = s.st_size>>(opt_complevel <= 1 ? 16 : opt_complevel + 17);
			if ( !threads ) threads = 1;
		}
		
		if ( opt_threads && (threads > opt_threads || opt_force) ) {
			threads = opt_threads;
		}
		
		if ( !(f=fopen(file[i], "rb")) ) {
			fprintf(stderr, "can't open '%s' for reading.\n", file[i]);
			exit(EXIT_FAILURE);
		}
		
		m = mmap(NULL, s.st_size, PROT_READ, MAP_SHARED|MAP_POPULATE, fileno(f), 0);
		if (m == MAP_FAILED) {
			perror("mmap failed");
			exit(EXIT_FAILURE);
		}
		madvise(m, s.st_size, MADV_SEQUENTIAL);
		fclose(f);
		
		ftemp = malloc(threads*sizeof(ftemp[0]));
		for ( p=0; p<threads; p++ ) {
			ftemp[p] = tmpfile();
		}
		
		if ( opt_verbose && !opt_stdout ) {
			printf("%s -> %ld thread%c: [", file[i], threads, threads != 1 ? 's' : ' ');
			fflush(stdout);
		}
		
#define BUFFSIZE 0x10000
#pragma omp parallel for private(p) num_threads(threads)
		for ( p=0; p<threads; p++ ) {
			off_t off = s.st_size*p/threads, pt;
			off_t len = p<threads-1 ? s.st_size/threads : s.st_size-(s.st_size*p/threads);
			uint8_t *mo;
			lzma_stream strm = LZMA_STREAM_INIT;
			lzma_ret ret;
			
			mo = malloc(BUFFSIZE);
			
			if ( lzma_easy_encoder(&strm, opt_complevel, LZMA_CHECK_CRC64) != LZMA_OK ) {
				fprintf(stderr, "unable to initialize LZMA encoder\n");
				exit(EXIT_FAILURE);
			}
			
			for (pt=0; pt<len; pt+=BUFFSIZE) {
				strm.next_in = &m[off+pt];
				strm.avail_in = len-pt >= BUFFSIZE ? BUFFSIZE : len-pt;
				strm.next_out = mo;
				strm.avail_out = BUFFSIZE;
				do {
					ret = lzma_code(&strm, LZMA_RUN);
					if ( ret != LZMA_OK ) {
						fprintf(stderr, "error in LZMA_RUN\n");
						exit(EXIT_FAILURE);
					}
					if ( BUFFSIZE - strm.avail_out > 0 ) {
						if ( !fwrite(mo, 1, BUFFSIZE - strm.avail_out, ftemp[p]) ) {
							perror("writing to temp file failed");
							exit(EXIT_FAILURE);
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
					fprintf(stderr, "error in LZMA_FINISH\n");
					exit(EXIT_FAILURE);
				}
				if ( BUFFSIZE - strm.avail_out > 0 ) {
					if ( !fwrite(mo, 1, BUFFSIZE - strm.avail_out, ftemp[p]) ) {
						perror("writing to temp file failed");
						exit(EXIT_FAILURE);
					}
					strm.next_out = mo;
					strm.avail_out = BUFFSIZE;
				}
			} while ( ret == LZMA_OK );
			lzma_end(&strm);
			
			free(mo);
			
			if (opt_verbose && !opt_stdout) {
				printf("%ld ", p);
				fflush(stdout);
			}
		}
		if (opt_verbose && !opt_stdout) {
			printf("] ");
		}
		munmap(m, s.st_size);
		
		if ( opt_stdout ) {
			for ( p=0; p<threads; p++ ) {
				fseek(ftemp[p], 0, SEEK_SET);
				while ( (rd=fread(buf, 1, sizeof(buf), ftemp[p])) > 0 ) {
					if ( write(STDOUT_FILENO, buf, rd) < 0 ) {
						perror("writing to standard output failed");
						exit(EXIT_FAILURE);
					}
				}
				if (rd < 0) {
					perror("reading from temporary file failed");
					exit(EXIT_FAILURE);
				}
			}
			free(ftemp);
			return 0;
		}
		
		snprintf(str, sizeof(str), "%s.xz", file[i]);
		if ( !(f=fopen(str,"wb")) ) {
			perror("error creating target archive");
			exit(EXIT_FAILURE);
		}
		
		new_action.sa_handler = term_handler;
		sigemptyset (&new_action.sa_mask);
		new_action.sa_flags = 0;
		
		sigaction(SIGINT, NULL, &old_action);
		if (old_action.sa_handler != SIG_IGN) sigaction(SIGINT, &new_action, NULL);
		sigaction(SIGHUP, NULL, &old_action);
		if (old_action.sa_handler != SIG_IGN) sigaction(SIGHUP, &new_action, NULL);
		sigaction(SIGTERM, NULL, &old_action);
		if (old_action.sa_handler != SIG_IGN) sigaction(SIGTERM, &new_action, NULL);
		
		for ( ts=p=0; p<threads; p++ ) {
			fseek(ftemp[p], 0, SEEK_SET);
			while ( (rd=fread(buf, 1, sizeof(buf), ftemp[p])) > 0 ) {
				if ( !fwrite(buf, 1, rd, f) ) {
					unlink(str);
					perror("writing to archive failed");
					exit(EXIT_FAILURE);
				} else ts += rd;
			}
			if (rd < 0) {
				perror("reading from temporary file failed");
				unlink(str);
				exit(EXIT_FAILURE);
			}
		}
		fclose(f);
		free(ftemp);
		
		if ( chmod(str, s.st_mode) ) {
			perror("warning: unable to change archive permissions");
		}

		u.actime = s.st_atime;
		u.modtime = s.st_mtime;
		
		if ( utime(str, &u) ) {
			perror("warning: unable to change archive timestamp");
		}
		
		sigaction(SIGINT, &old_action, NULL);
		sigaction(SIGHUP, &old_action, NULL);
		sigaction(SIGTERM, &old_action, NULL);
		
		if ( opt_verbose ) {
			printf("%ld -> %ld %3.3f%%\n", s.st_size, ts, ts*100./s.st_size);
		}
		
		if ( !opt_keep ) unlink(file[i]);
	}
	
	return 0;
}
