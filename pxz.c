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
#include <omp.h>
#include <lzma.h>

FILE **ftemp;
char str[0x100];

void term_handler( int sig __attribute__ ((unused)) ) {
	unlink(str);
}

int main( int argc, char **argv ) {
	int i, files;
	uint64_t p, procs;
	struct stat s;
	uint8_t *m;
	char buf[0x10000];
	char xzcmd[0x1000] = "/usr/bin/xz ";
	FILE *f, *fp;
	ssize_t rd;
	struct sigaction new_action, old_action;
	
	for (files=0, i=1; i<argc; i++) {
		if (argv[i][0] != '-') {
			files++;
		} else {
			strncat(xzcmd, argv[i], sizeof(xzcmd));
		}
	}
	
	if (!files) {
		execvp("/usr/bin/xz", argv);
		perror("xz execution failed");
		exit(EXIT_FAILURE);
	}

	for (i=1; i<argc; i++) {
		if (argv[i][0] != '-') {
			if ( stat(argv[i], &s)) {
				fprintf(stderr, "Can't stat '%s'.\n", argv[1]);
				return 1;
			}
#ifdef _OPENMP
			procs = omp_get_max_threads();
#else
			procs = 1;
#endif
			if ( s.st_size < 8<<20 ) {
				fprintf(stderr, "File too small wrt dictionary size, reducing # of threads to 1.\n");
				procs = 1;
			}
			
			if ( !(f=fopen(argv[i], "rb")) ) {
				fprintf(stderr, "Can't open '%s' for reading.\n", argv[i]);
				return 1;
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

#pragma omp parallel for private(p)
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
					perror ("Fork failed");
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
					perror ("Fork failed");
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
	
			snprintf(str, sizeof(str), "%s.xz", argv[i]);
			f=fopen(str,"wb");

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
			
			unlink(argv[i]);
		}
	}
	
	return 0;
}
