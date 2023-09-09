#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT_LENGTH 1024

#define bool uint8_t
#define true 1
#define false 0

char** path;
int num_paths;

void displayPrompt() {
    printf("witsshell> ");
    fflush(stdout);
}

void print_error(){
	char error_message[30] = "An error has occurred\n";
	write(STDERR_FILENO, error_message, strlen(error_message));
}

void strip_extra_spaces(char* string) {
  size_t i, x;

  for(i=x=0; string[i]; ++i) {
    if(string[i] != ' ' || (i > 0 && string[i-1] != ' ')) {
      string[x++] = string[i];
    }
  }

  string[x] = '\0';

  size_t end = strlen(string) - 1;
  while(string[end] == ' ') {
    --end;
  }
  string[end + 1] = '\0';
}

void executeCommand(char *command) {
    strip_extra_spaces(command);

    if (strlen(command) == 1 && command[0] == '&') {
        return;
    }

	//support 50 parallel commands
	char* parallel_cmd[50];
	int num_parallel_cmd = 0;
	char* current_cmd = strtok(command, "&");
	pid_t child_pids_array[50];

	while(current_cmd != NULL){
		strip_extra_spaces(current_cmd);
		parallel_cmd[num_parallel_cmd] = current_cmd;
		num_parallel_cmd++;
		current_cmd = strtok(NULL, "&");
	}

	for(int k = 0; k < num_parallel_cmd; k++){
		//redirection
		char* found_redirection_char = strchr(parallel_cmd[k], '>');
		char* output_file = NULL;
		if (found_redirection_char != NULL){
			char red_arg[5];
			//output out
			char* cmd_temp = strtok(parallel_cmd[k], ">");
			output_file = strtok(NULL, ">");
			char* extra_redirect_found = strtok(NULL, ">");
			
			if(output_file == NULL || strlen(output_file) == 0 || cmd_temp == NULL || strlen(cmd_temp) == 0){
				print_error();
				return;
			}
			strip_extra_spaces(cmd_temp);
			strip_extra_spaces(output_file);
			
			if (extra_redirect_found != NULL || strchr(output_file, ' ') != NULL) { 
				print_error();
				return;
			}
			parallel_cmd[k] = cmd_temp;
		}

		//max amount of args willing to handle
		char* args[20];
		int argCount = 0;

		// Tokenize the command
		char* token = strtok(parallel_cmd[k], " ");
		while (token != NULL) {
			args[argCount++] = token;
			token = strtok(NULL, " ");
		}
		args[argCount] = NULL; // Null-terminate the argument list

		if (argCount == 0) {
			return;
		}

		// exit command
		if (argCount > 0 && strcmp(args[0], "exit") == 0) {
			if (argCount > 1) {
				print_error();
			} else {
				exit(EXIT_SUCCESS);
			}
			return; // No need to execute subsequent commands
		}

		if (argCount > 0 && strcmp(args[0], "cd") == 0) {
			// It's a "cd" command
			if (argCount != 2) {
				// Usage: cd <directory>
				print_error();
			} else {
				// Attempt to change the current directory
				chdir(args[1]);
			}
			return; // No need to execute subsequent commands
		}

		if (argCount > 0 && strcmp(args[0], "path") == 0) {
			free(path);
			// We want however many arguments size of pointers
			path = malloc((argCount - 1) * sizeof(char*));
			num_paths = argCount - 1;

			for (int i = 1; i < argCount; i++) {
				// Now we create the actual address of memory the pointer points to
				path[i - 1] = malloc((strlen(args[i]) + 1) * sizeof(char));
				// copied it into the path
				strcpy(path[i - 1], args[i]);
			}
			return;
		}

		// if here then external command
		// now we search through the paths
		// we execute this only if it is in the child so that we don't replace the mother
		pid_t fork_result = fork();

		// Note that 0 is the child and anything else is the mother
		if (fork_result == 0) {
			if (output_file != NULL){
				FILE* write_file = fopen(output_file, "w"); 
				//dup2 changes file numbers to point to our new file instead of stdout
				dup2(fileno(write_file), fileno(stdout));
			}

			bool found_binary = false;
			for (size_t i = 0; i < num_paths; i++) {
				// we need the length of our path and the appending of the command we wish to call
				// The 1 accommodates for C
				int bin_path_len = strlen(path[i]) + strlen(args[0]) + 2;
				char* bin_path = malloc(bin_path_len * sizeof(char));
				strcpy(bin_path, path[i]);
				strcat(bin_path, "/");
				strcat(bin_path, args[0]);

				// do we have executable access (can we run it)
				if (access(bin_path, X_OK) == 0) {
					// this takes the args of the command (ls -a) and NOT the other possible file directories
					execv(bin_path, args);
					found_binary = true;
					break;
				}
			}

			if (found_binary == false) {
				print_error();
			}

			// error
			exit(EXIT_FAILURE); // Terminate the child with an error
		} else {
			// we wait for the child to be done so it stops being a mother
			// NULL - pointer to the return value of the child
			// 0 - option that means ignore the return
			child_pids_array[k] = fork_result;
			
		}
	}

	for (size_t j = 0; j < num_parallel_cmd; j++)
	{
		int status;
		waitpid(child_pids_array[j], &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
				// Child terminated with an error
				return; // No need to execute subsequent commands
			}
	}
}


int main(int argc, char *argv[]) {
    path = malloc(1*sizeof(char*));
	//Set the default as bin
	path[0] = "/bin/";
	num_paths = 1;
	FILE *inputFile = NULL;
    char input[MAX_INPUT_LENGTH+1];

    if (argc == 1) {
        // Interactive mode
        while (1) {
            displayPrompt();
            if (fgets(input, sizeof(input), stdin) == NULL) {
                // Handle Ctrl+D (end-of-file) to exit the shell
                printf("\n");
                exit(EXIT_SUCCESS);
            }
            //input[strlen(input) - 1] = '\0';  // Remove newline character
			input[strcspn(input, "\n")] = 0;
            executeCommand(input);
        }
    } else if (argc == 2) {
        // Batch mode
        inputFile = fopen(argv[1], "r");
        if (inputFile == NULL) {
            print_error();
            exit(EXIT_FAILURE);
        }

        while (fgets(input, sizeof(input), inputFile)) {
            //input[strlen(input) - 1] = '\0';  // Remove newline character
            input[strcspn(input, "\n")] = 0;
			executeCommand(input);
        }

        fclose(inputFile);
    
	} else {
        print_error();
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}