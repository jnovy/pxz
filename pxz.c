/*
 * Parallel XZ 0.0.1, runs LZMA compression simultaneously on multiple cores.
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
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <getopt.h>
#include <omp.h>
#include <lzma.h>

#define XZ_BINARY "/usr/bin/xz"
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
char *xzcmd;
size_t xzcmd_max;

int opt_complevel = 6, opt_stdout, opt_keep, opt_threads;
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
			case 'e':
			case 'q':
			case 'v':
			case 'Q':
				ADD_OPT(c);
				break;
			case 'k':
				opt_keep = 1;
				break;
			case 'T':
				opt_threads = atoi(optarg);
				break;
			case 'V':
			case 'd':
			case 't':
			case 'l':
			case 'h':
			case 'H':
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
	uint64_t p, procs;
	struct stat s;
	uint8_t *m;
	char buf[0x10000];
	FILE *f, *fp;
	ssize_t rd;
	struct sigaction new_action, old_action;
	
	xzcmd_max = sysconf(_SC_ARG_MAX);
	xzcmd = malloc(xzcmd_max);
	snprintf(xzcmd, xzcmd_max, XZ_BINARY);
	
	parse_args(argc, argv);
	
	for (i=0; i<files; i++) {
#ifdef _OPENMP
			procs = omp_get_max_threads();
#else
			procs = 1;
#endif
			if ( opt_threads > 0 && procs > opt_threads ) procs = opt_threads;
			
			if ( stat(file[i], &s)) {
				fprintf(stderr, "can't stat '%s'.\n", file[i]);
				exit(EXIT_FAILURE);
			}
			
			if ( opt_complevel >= 3 && s.st_size < (1<<(opt_complevel-3))<<20 ) {
				procs = 1;
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
			
			ftemp = malloc(procs*sizeof(ftemp[0]));
			for ( p=0; p<procs; p++ ) {
				ftemp[p] = tmpfile();
			}
#pragma omp parallel for private(p) num_threads(procs)
			for ( p=0; p<procs; p++ ) {
				int status;
				int pid0, pid1;
				int pipe_fd[2];
				off_t off = s.st_size*p/procs;
				off_t len = p<procs-1 ? s.st_size/procs : s.st_size-(s.st_size*p/procs);
				
				if (pipe(pipe_fd) < 0) {
					perror ("pipe failed");
					exit (EXIT_FAILURE);
				}
				
				if ((pid0=fork()) < 0) {
					perror ("fork failed");
					exit(EXIT_FAILURE);
				}
				
				if (!pid0) {
					close(pipe_fd[0]);
					dup2(pipe_fd[1], 1);
					close(pipe_fd[1]);
					if (write(1, &m[off], len) < 0) {
						perror("write to pipe failed");
						exit(EXIT_FAILURE);
					}
					exit(EXIT_SUCCESS);
				}
				
				if ((pid1=fork()) < 0) {
					perror ("fork failed");
					exit(EXIT_FAILURE);
				}
				
				if (!pid1) {
					close(pipe_fd[1]);
					dup2(pipe_fd[0], 0);
					close(pipe_fd[0]);
					
					fp = popen(xzcmd, "r");
					while ( (rd=fread(buf, 1, sizeof(buf), fp)) > 0 ) {
						fwrite(buf, 1, rd, ftemp[p]);
					}
					if (rd < 0) {
						perror("reading from pipe failed");
						exit(EXIT_FAILURE);
					}
					pclose(fp);
					
					exit(EXIT_SUCCESS);
				}
				
				close(pipe_fd[0]);
				close(pipe_fd[1]);
				
				waitpid (pid1, &status, 0);
			}
			
			munmap(m, s.st_size);
			
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
			
			for ( p=0; p<procs; p++ ) {
				fseek(ftemp[p], 0, SEEK_SET);
				while ( (rd=fread(buf, 1, sizeof(buf), ftemp[p])) > 0 ) {
					fwrite(buf, 1, rd, f);
				}
				if (rd < 0) {
					perror("reading from temporary file failed");
					exit(EXIT_FAILURE);
				}
			}
			fclose(f);
			free(ftemp);
			
			sigaction(SIGINT, &old_action, NULL);
			sigaction(SIGHUP, &old_action, NULL);
			sigaction(SIGTERM, &old_action, NULL);
			
			if ( !opt_keep ) unlink(file[i]);
	}
	
	return 0;
}
