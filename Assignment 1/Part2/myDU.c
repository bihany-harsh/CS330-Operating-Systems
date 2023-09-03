#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>


// off_t calculate_size(char *dir_path) {
//     DIR* dir;
//     struct dirent *fd;
//     struct stat file_stat;
//     char fullpath[1024];


//     off_t size_of_dir = 0;

//     if (lstat(dir_path, &file_stat) == 0) {
//         size_of_dir += file_stat.st_size;
//     }

//     dir = opendir(dir_path);

//     if (dir) {
//         while ((fd = readdir(dir)) != NULL) {
// 			strcpy(fullpath, dir_path);

//             if (!strcmp(fd->d_name, "..") || !strcmp(fd->d_name, ".")) {
//                 continue;
//             }
// 			strcat(fullpath, "/");
// 			strcat(fullpath, fd->d_name);

//             lstat(fullpath, &file_stat);

//             if (S_ISDIR(file_stat.st_mode)) {
//                 size_of_dir += calculate_size(fullpath);
//             } else {
//                 size_of_dir += file_stat.st_size;
//             }
//         }
//         if (closedir(dir) != 0) {
//             perror("closedir");
//         }
//     } else {
//         perror("opendir");
//     }

//     return size_of_dir;
// }

// int main(int argc, char* argv[])
// {
//     if (argc == 2) {
// 		printf("%lu\n", calculate_size(argv[1]));
//     } else {
//         printf("Require exactly 2 arguments\n");
//     }

//     return 0;
// }

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Require at least 1 argument\n");
        return 1;
    }
    DIR *dir;
    off_t size_of_dir = 0;
    struct dirent *dir_entry;
    struct stat file_stat;
    char fullpath[1024];

    if (stat(argv[1], &file_stat) == -1) {
        perror("stat");
        exit(1);
    }
    size_of_dir += file_stat.st_size;

    dir = opendir(argv[1]);
    if (dir) {
        while((dir_entry = readdir(dir)) != NULL) {
			strcpy(fullpath, argv[1]);

            if (!strcmp(dir_entry->d_name, "..") || !strcmp(dir_entry->d_name, ".")) {
                continue;
            }

			strcat(fullpath, "/");
			strcat(fullpath, dir_entry->d_name);

            if (stat(fullpath, &file_stat) == -1) {
                perror("stat");
                continue;
            }

            if (S_ISDIR(file_stat.st_mode)) {
                int fd[2];
                if (pipe(fd) < 0) {
                    perror("pipe");
                    exit(-1);
                }

                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    exit(-1);
                }

                if (pid == 0) { /* Child */
                    close(fd[0]); 			// close the read end
					close(1);				// close the STDOUT of the child
					if (dup(fd[1]) == -1) { // configuring output of child to pipe
						perror("dup");
						exit(-1);
					} 		
                    char* new_argv[3];
                    new_argv[0] = argv[0];
                    new_argv[1] = fullpath;
                    new_argv[2] = NULL;
                    
                    if (execvp(argv[0], new_argv) < 0) {
                        perror("exec");
                        exit(-1);
                    }
                } else { 
                    close(fd[1]); 			// close the write end of the parent
                    close(0);
                    if (dup(fd[0]) == -1) {
                        perror("dup");
                        exit(-1);
                    }
                    char buf[32];
                    read(0, buf, sizeof(buf));
                    size_of_dir += atoll(buf); 
                }
            } else {
                size_of_dir += file_stat.st_size;
            }
        }
        if (closedir(dir) != 0) {
            perror("closedir");
        }
    } else {
        perror("opendir");
    }

    char buf[32];
	sprintf(buf, "%lu", size_of_dir);
    write(1, buf, strlen(buf));
    
    if (argc == 2) {
        printf("\n");
    }
    exit(0);
}
