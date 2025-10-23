#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>

#include <vector>
#include <string>

#include "Tokenizer.h"

// all the basic colours for a shell prompt
#define RED     "\033[1;31m"
#define GREEN	"\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE	"\033[1;34m"
#define WHITE	"\033[1;37m"
#define NC      "\033[0m"

using namespace std;

string previousDir = "";  // to hold previous directory for "cd -"
vector<pid_t> background_pids;  // vector to hold background process PIDs

int main () {
    for (;;) {
        // check background processes for completion
        for (auto it = background_pids.begin(); it != background_pids.end();) {
            int status;
            pid_t result = waitpid(*it, &status, WNOHANG);
            if (result == 0) {
                ++it;  // process is still running
            } else if (result == -1) {
                perror("waitpid error");
                it = background_pids.erase(it);  // remove from list on error
            } else {
                cerr << GREEN << "Background process " << *it << " completed." << NC << endl;
                it = background_pids.erase(it);  // remove completed process from list
            }
        }
        // need date/time, username, and absolute path to current dir
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd error");
            continue;
        }
        string currentPath(cwd);
        
        const char* user = getenv("USER");
        string username = (user != nullptr) ? string(user) : "unknown";

        time_t rawtime;
        time(&rawtime);
        string timeStr = string(ctime(&rawtime));
        if (!timeStr.empty() && timeStr.back() == '\n') {
            timeStr.pop_back();
        }
        // print shell prompt
        cout << GREEN << timeStr << " "
                << username << ":"
                << currentPath << NC
                << YELLOW << "$ " << NC;

        // get user inputted command
        string input;
        getline(cin, input);


        if (input == "exit") {  // print exit message and break out of infinite loop
            cout << RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            break;
        }

        // get tokenized commands from user input
        Tokenizer tknr(input);
        if (tknr.hasError()) {  // continue to next prompt if input had an error
            continue;
        }

        // print out every command token-by-token on individual lines
        // prints to cerr to avoid influencing autograder
        for (auto cmd : tknr.commands) {
            for (auto str : cmd->args) {
                cerr << "|" << str << "| ";
            }
            if (cmd->hasInput()) {
                cerr << "in< " << cmd->in_file << " ";
            }
            if (cmd->hasOutput()) {
                cerr << "out> " << cmd->out_file << " ";
            }
            cerr << endl;
        }

        if(tknr.commands.empty()){
            continue;
        }

        vector<string>& args = tknr.commands.at(0)->args;

        // handle built-in "cd" command
        if (args.at(0) == "cd") {
            string targetPath;
            char oldCwd[PATH_MAX];
            string currentDir;

            if(getcwd(oldCwd, sizeof(oldCwd)) == NULL) {
                perror("getcwd oldCwd error");
                continue;
            }
            else{
                currentDir = string(oldCwd);
            }
  
            // handle no argument case : go to home directory
            if (args.size() < 2) {
                cerr << "cd: no argument case" << endl;
                const char* homeDir = getenv("HOME");
                if (homeDir == NULL) {
                    cerr << "cd: HOME not set" << endl;
                    continue;
                }
                else{
                    targetPath = homeDir;
                }
            }
            else if (tknr.commands.at(0)->args.at(1) == "-") {// change to previous directory if argument is "-"
                if (previousDir == "") {
                    cerr << "cd: previousDir not set" << endl;
                    continue;
                }
                else{
                    targetPath = previousDir;
                    cerr << "cd: changing to previous directory " << previousDir << endl;
                }
            }
            else {
                targetPath = args.at(1);
            }

            if (chdir(targetPath.c_str()) != 0) {  // error check
                perror("cd : chdir error");
                continue;
            }
            else {  // update previousDir if cd was successful
                previousDir = currentDir;
                cerr << "cd: changed directory to " << targetPath << endl;
            }
            continue;
        }

        // save original stdin to restore later
        int origStdin = dup(STDIN_FILENO);
        if(origStdin == -1){
            perror("dup origStdin error");
            continue;
        }

        int numCommands = tknr.commands.size();
        pid_t lastPid = -1; // to hold PID of last command for waiting later
        int pipeInFd = STDIN_FILENO;  // initial input is from stdin

        // set up pipes for multiple commands
        for(int i = 0; i < numCommands; i++){
            Command* currCmd = tknr.commands.at(i);
            bool isLast = (i == numCommands - 1);

            int pipeFd[6]; 

            if(!isLast){
                if(pipe(pipeFd) == -1){
                    perror("pipe error");
                    break;
                }
            }
        }

        // fork to create child
        pid_t pid = fork();
        if (pid < 0) {  // error check
            perror("fork");
            exit(2);
        }

        if (pid == 0) {  // if child, exec to run command
            // run single commands with no arguments
            vector<string>& args = tknr.commands.at(0)->args;
            vector<char*> cArgs;
            for (auto& s : args) {
                cArgs.push_back((char*)(s.c_str()));
            }

            if (execvp(args[0], args) < 0) {  // error check
                perror("execvp");
                exit(2);
            }
        }
        else {  // if parent,
            Command* currCmd = tknr.commands.at(0);
            if (currCmd->isBackground()) {  // if background process, do not wait
                background_pids.push_back(pid);
                cerr << "[" << pid << "] Running in background." << endl;
            }
            else {  // wait for foreground process to finish
                int status = 0;
                waitpid(pid, &status, 0);
                if(status > 1){ 
                    cerr << "Process exited with status: " << status << endl;
                    exit(status);
                }
            }
        }
    }
}