#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>

// Additional includes
#include <time.h>
#include <ctype.h>

// Color definations for printf colorizing
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

// Constant system name
const char * sysname = "shellington";

//Variables for storing data for short command.
char **alias, **wd;
int *saveCount;

// Counter for custom command rps
int *rps_counter;

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};
/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL) {
		command->name[0]=0;
	}
	else {
		strcpy(command->name, pch);
	}
	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
			|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


    //FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
  	while (1)
  	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
  	}
  	if (index>0 && buf[index-1]=='\n') // trim newline from the end
  		index--;
  	buf[index++]=0; // null terminate string

  	strcpy(oldbuf, buf);

  	parse_command(buf, command);

  	// print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  	return SUCCESS;
}

// Dynamic memory allocating used to store data of the short command.
void mallocShort(){
	int i; int j;
	alias = (char**)malloc(sizeof(char*)*50);
    for(i=0; i<50; i++)
    {
       alias[i] = (char*)malloc(sizeof(char)*50);
    }

	wd = (char**)malloc(sizeof(char*)*50);
    for(j=0; j<50; j++)
    {
       wd[j] = (char*)malloc(sizeof(char)*50);

    }
	saveCount = (int*)malloc(sizeof(int));
	(*saveCount) = 0;
}
//Malloc to keep the scores, user score is index0 and shellington score is index1
void malloc_rps() {
	rps_counter = (int*)malloc(sizeof(int)*2); 
}
// Freeing the allocated space for data storage units of short command.
void freeShort() {
	int i; int j;
	for(i=0; i<50; i++)
    {
       free(alias[i]);
    }
	for(j=0; j<50; j++)
    {
       free(wd[i]);
    }
	free(alias);
	free(wd);
	free(saveCount);
}
// Free allocated space for custom command
void free_rps() {
	free(rps_counter);
}
int process_command(struct command_t *command);
int main()
{
	malloc_rps(); // Malloc for rps custom command
	mallocShort(); // Calling the function the allocate space for short command.
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}
	free_rps(); // Free allocated space for rps custom command
	freeShort(); // Freeing space allocated for the short command.
	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}

	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
	    // extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		//Creating the destination string for execv() command
		char bin[40] = "/bin/";
		strcat(bin, command->name);

		// increase args size by 2
		command->args=(char **)realloc(
			command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		// Check if short is called, if so skip child process.
		if(strcmp(command->name, "short") == 0) {
			exit(0);
		}
		//Check if rps is called, if so close the child process.
		else if(strcmp(command->name, "rps") == 0) {
			exit(0);
		}
		// Check if the command is remindme.
		else if(strcmp(command->name, "remindme") == 0){
			if (command->arg_count < 4) { // Exit if the args are insufficient.
				printf("Not enough arguments.\n");
				exit(0);
			}
			FILE *crontab_ptr = NULL; 
			crontab_ptr = fopen("crontabs.txt", "a+"); //Create a .txt file to store data.
			if(crontab_ptr == NULL) {
				printf("Error reaching file.\n");
				exit(0);
			}

			int var_i = 2;
			char *hours = NULL;
			char *minutes = NULL;
			char *cron_command = "/bin/crontab";
			char *crontab_args[3];
			char *time = strdup(command->args[1]);
			crontab_args[0] = strdup("/bin/crontab");
			crontab_args[1] = strdup("crontabs.txt");
			crontab_args[2] = NULL;

			//Extra command, if user inputs remindme remove all, remove all crontabs and delete the .txt file.
			if(strcmp(command->args[1], "remove") == 0 && strcmp(command->args[2], "all") == 0) {	
				remove("crontabs.txt");
				crontab_args[1] = strdup("-r");
				execv(cron_command, crontab_args);
				exit(0);
			}

			//Splitting the first arg to get the time.
			int token_count = 0;
			char *token = strtok(time, ":");
			while (token != NULL) {
				if(token_count == 0)
					hours = strdup(token);
				if(token_count == 1)
					minutes = strdup(token);
				token = strtok(NULL, ":");
				token_count++;
			}

			// Print error if the time format was wrong.
			if((hours == NULL) || (minutes == NULL)) {
				printf("Invalid format, please use format such as 14:30.\n");
				exit(0);
			}

			// Writing data to .txt file to pass on to crontab.
			fputs(minutes, crontab_ptr);
			fputs(" ", crontab_ptr);
			fputs(hours, crontab_ptr);
			fputs(" * * *  XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send ", crontab_ptr);
			while (command->args[var_i] != NULL) {
				fputs((command->args[var_i]), crontab_ptr);
				fputs(" ", crontab_ptr);
				var_i++;	
			}
			fputs("\n", crontab_ptr);
			fclose(crontab_ptr);
			// Executing crontab with the .txt file we created.
			execv(cron_command, crontab_args); 
			exit(0);
		}

		// Check if bookmark is called, this function works with reading and writing to a .txt file.
		else if(strcmp(command->name, "bookmark") == 0){
			FILE *fptr = NULL;
			FILE *ftemp = NULL;

			if(strcmp(command->args[1], "-l") == 0){ // Printing the current bookmarks.
				char buffer[256];
				int line = 0;
				fptr = fopen("bookmarks.txt", "r");
				if(fptr == NULL) {
					printf("Error opening bookmarks.\n");	
					exit(0);
				}
				while (fgets(buffer, 256, fptr)){
					printf("\t%d %s", line, buffer);
					line++;
				}
				fclose(fptr);
			}
			
			else if(strcmp(command->args[1], "-d") == 0){ // Deleting the bookmark according to given index.
				int index = atoi(command->args[2]);
				int line = 0;
				char buffer[256];
				int currentLine = 0;
				fptr = fopen("bookmarks.txt", "r");
				ftemp = fopen("temp.txt", "a+");		// Creating a temp .txt file.
				if(fptr == NULL) {						
					printf("Error deleting bookmarks.\n");	
					exit(0);
				}
				if(ftemp == NULL) {
					printf("Error deleting bookmark.\n");	
					exit(0);
				}
				while (fgets(buffer, 256, fptr)){	//Copying the original text to temp text file.
					if(currentLine == index) {
						currentLine++;
						continue;
					}
					fputs(buffer, ftemp);
					currentLine++;
				}
				fclose(fptr);
				fclose(ftemp);
				remove("bookmarks.txt");				//Deleting the original one and renaming the temp to intended name.
				rename("temp.txt", "bookmarks.txt");
			}
			
			else if(strcmp(command->args[1], "-i") == 0){
				struct command_t *bookmarkCommand=malloc(sizeof(struct command_t));	// Use command struct to generate a command from txt.
				int index = atoi(command->args[2]);	
				int count = 0;
				char *dir;
				char buffer[256];
				char bookmarkBin[256] = "/bin/";
				int i, j, len;
				fptr = fopen("bookmarks.txt", "r");
				if(fptr == NULL) {
					printf("Error opening bookmarks.\n");	
					exit(0);
				}
				while (fgets(buffer, 256, fptr)){	//Getting the line user wants to execute.
					if(count == index) {
						dir = strdup(buffer);
						break;
					}
					count++;
				}
				len = strlen(dir);					//Removing the " and \n from the command we got.
				for(i=0; i<len; i++) {
        			if((dir[i] == '\"') || (dir[i] == '\n')) {		
            			for(j=i; j<len; j++){	
                		dir[j] = dir[j+1];
            			}
            		len--;
            		i--;
        			}
    			}
				parse_command(dir, bookmarkCommand);	// Using the already defined parser.

				// increase args size by 2
				bookmarkCommand->args=(char **)realloc(
				bookmarkCommand->args, sizeof(char *)*(bookmarkCommand->arg_count+=2));

				// shift everything forward by 1
				for (int i=bookmarkCommand->arg_count-2;i>0;--i)
				bookmarkCommand->args[i]=bookmarkCommand->args[i-1];

				// set args[0] as a copy of name
				bookmarkCommand->args[0]=strdup(bookmarkCommand->name);
				// set args[arg_count-1] (last) to NULL
				bookmarkCommand->args[bookmarkCommand->arg_count-1]=NULL;

				strcat(bookmarkBin, bookmarkCommand->name);
				execv(bookmarkBin, bookmarkCommand->args);	//Using execv to execute the commands.
				free(bookmarkCommand);
				fclose(fptr);
			}

			else{
				int i = 1;				
				fptr = fopen("bookmarks.txt", "a+");
				if(fptr == NULL) {
					printf("Error inserting bookmark.\n");	
					exit(0);
				}
				while(command->args[i] != NULL) {
					fputs(command->args[i], fptr);
					fputs(" ", fptr);
					i++;
				}
				fprintf(fptr, "\n");
				fclose(fptr);
			}
			exit(0);
		}
		else {
			execv(bin, command->args); // Executing UNIX commands here
			exit(0);
		}
	}
	//Parent process.
	else
	{
		if (!command->background) wait(0); // wait for child process to finish
		
		// Check if short custom command is called.
		if(strcmp(command->name, "short") == 0) {
			if(strcmp(command->args[0], "set") == 0) { // Check if first args is: set
				int j;
				char cwd[1024];	
				getcwd(cwd, sizeof(cwd));	// Getting the current working directory.

				for(j = 0; j < (*saveCount); j++){ // Iterating to see if the alias is already saved. If so, update the directory and keep the alias.
					if(strcmp(command->args[1], alias[j]) == 0){
						wd[j] = strdup(cwd);
						printf("An alias named %s already has been found, overriding the path.\n", alias[j]);	
						return SUCCESS;
					}
				}
				alias[(*saveCount)] = strdup(command->args[1]);	// If alias is new, save to the storage.
				wd[(*saveCount)] = strdup(cwd);
				(*saveCount)++;	// Increment to keep track of how many pairs we have.
				printf("New alias %s is saved. Curent alias number: %d\n", command->args[1], *saveCount);
			}
			if(strcmp(command->args[0], "jump") == 0) { // Check if first arg is: jump
				int i;
				for(i = 0; i < (*saveCount); i++){
					if(strcmp(command->args[1], alias[i]) == 0) {	// If alias is saved, jump to the saved directory.
						chdir(wd[i]);	
						printf("Alias %s found, changing dir.\n", alias[i]);
						return SUCCESS;
					}
				}
				printf("There is no such alias as %s, try again.\n", command->args[1]); 	//If there is no alias saved, print error.
			}
		}

		//Check if custom command rps is called. (Short for Rock, Paper, Scissors)
        else if (strcmp(command->name, "rps") == 0) {
			if(command->arg_count == 0) {
				printf("Not enough arguments given, try again.\n"); // Return if args are not enough
				return UNKNOWN;
			}
			int i;
			srand(time(NULL)); 
			int random_var = rand() % 3;  // Random number generator for shellington to determine the move.
			char *moves[3];				  // Array containing move names.
			moves[0] = strdup("rock");	
			moves[1] = strdup("paper");
			moves[2] = strdup("scissors");
			char *linux_move = strdup(moves[random_var]); 	// Picking the move for shellington via random generated number.
			char *user_move = strdup(command->args[0]);		// Getting the user input.
			for(i = 0; i < strlen(user_move); i++) {
				user_move[i] = tolower(user_move[i]);		// lowercase convertion to compare moves.
			}
            printf("Rock!\n");								
			sleep(1);
			printf("Paper!\n");
			sleep(1);
			printf("Scissors!\n");
			sleep(1);
			printf("You said: %s!\nShellington said %s!\n", user_move, linux_move);

			//Checking which move wins over the other, printing the result and storing the data.
			if(strcmp(user_move, moves[0]) == 0) {	
				if(random_var == 0) printf(COLOR_YELLOW "That's a tie!\n" COLOR_RESET);
				if(random_var == 1) {
					printf(COLOR_RED "Paper beats rock! You lost.\n" COLOR_RESET);
					(*(rps_counter+1))++;	//Incrementing the 1st index of the pointer if user loses.
					}
				if(random_var == 2) {
					printf(COLOR_GREEN "Rock beats Scissors! You won.\n" COLOR_RESET);
					(*rps_counter)++;	// Incrementing the 0th index of the pointer if user wins.
				}
			}
			else if(strcmp(user_move, moves[1]) == 0) {
				if(random_var == 1) printf(COLOR_YELLOW "That's a tie!\n" COLOR_RESET);
				if(random_var == 0) {
					printf(COLOR_GREEN "Paper beats rock! You won.\n" COLOR_RESET);
					(*rps_counter)++;
					}
				if(random_var == 2) {
					printf(COLOR_RED "Scissors beats paper! You lost.\n" COLOR_RESET);
					(*(rps_counter+1))++;
					}
				}
			else if(strcmp(user_move, moves[2]) == 0) {
				if(random_var == 2) printf(COLOR_YELLOW "That's a tie!\n" COLOR_RESET);
				if(random_var == 0) {
					printf(COLOR_RED "Rock beats Scissors! You lost.\n" COLOR_RESET);
					(*(rps_counter+1))++;
					}
				if(random_var == 1) {
					printf(COLOR_GREEN "Scissors beats paper! You won.\n" COLOR_RESET);
					(*rps_counter)++;
					}
				}
			else {
				printf("That move does not exist! Try again.\n");
				return UNKNOWN;		
			}
			printf("SCOREBOARD: You %d, Shellinton %d\n", *rps_counter, *(rps_counter+1));	
        }
		return SUCCESS;
	}
	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}