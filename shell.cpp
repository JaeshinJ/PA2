#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

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

        // handle built-in "cd" command
        if (tknr.commands.at(0)->args.at(0) == "cd") {
            if(tknr.commands.size() > 1){
                cerr << "Error: 'cd' command cannot be used in a pipeline" << endl;
                continue;
            }

            vector<string>& args = tknr.commands.at(0)->args;
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
            else if (args.at(1) == "-") {// change to previous directory if argument is "-"
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
            }
            else {  // update previousDir if cd was successful
                previousDir = currentDir;
                if(args.size() >= 2 && args.at(1) == "-"){
                    cerr << targetPath << endl; // 'cd -' print changed dir
                }
            }
            continue;
        }

        // save original stdin to restore later
        int origStdin = dup(STDIN_FILENO);
        if(origStdin == -1){
            perror("dup origStdin error");
            continue;
        }
        int origStdout = dup(STDOUT_FILENO);
        if(origStdout == -1){
            perror("dup origStdout error");
            close(origStdin); // close backuped stdin
            continue;
        }


        int numCommands = tknr.commands.size();
        int pipeInFd = STDIN_FILENO;
        pid_t lastPid = -1; // to hold PID of last command for waiting later
        vector<pid_t> pipePids;

        // set up pipes for multiple commands
        for(int i = 0; i < numCommands; i++){
            Command* currCmd = tknr.commands.at(i);
            bool isLast = (i == numCommands - 1);

            // pideFD : [0] = read, [1] = write
            int pipeFd[2]; 
            if(!isLast){
                if(pipe(pipeFd) == -1){
                    perror("create pipe error");
                    break;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {  // error check
                perror("fork error");
                break; // fork fail
            }

            // fork to create child
            if (pid == 0) {  // if child, exec to run command
                if(currCmd->hasInput()){
                    int fdIn = open(currCmd->in_file.c_str(), O_RDONLY);
                    if(fdIn < 0){
                        perror("open input file error");
                        exit(1);
                    }
                    if(dup2(fdIn, STDIN_FILENO) < 0){
                        perror("dup2 input error");
                        exit(1);
                    }
                    close(fdIn);
                } else if(pipeInFd != STDIN_FILENO){
                    if(dup2(pipeInFd, STDIN_FILENO) < 0)
                    {
                        perror("dup2 stdin error");
                        exit(1);
                    }
                }

                if(currCmd->hasOutput()){
                    int fdOut = open(currCmd->out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if(fdOut < 0){
                        perror("open output file error");
                        exit(1);
                    }
                    if(dup2(fdOut, STDOUT_FILENO) < 0){
                        perror("dup2 output error");
                        exit(1);
                    }
                    close(fdOut);
                } else if (!isLast) {
                    if(dup2(pipeFd[1], STDOUT_FILENO) < 0){
                        perror("dup2 pipeFd[1] error");
                        exit(1);
                    }
                }

                // child must close both end of pipe
                if(!isLast){
                    close(pipeFd[0]);
                    close(pipeFd[1]);
                }

                // run single commands with no arguments
                vector<string>& currArgs = currCmd->args;
                vector<char*> cArgs;
                for (auto& s : currArgs) {
                    cArgs.push_back((char*)(s.c_str()));
                }
                cArgs.push_back(nullptr);

                if (execvp(cArgs[0], cArgs.data()) < 0) {  // error check
                    perror(cArgs[0]);
                    exit(2);
                }

                exit(0);
            } else {  // if parent,
                pipePids.push_back(pid);
                if(isLast){
                    lastPid = pid;
                }

                if(pipeInFd != STDIN_FILENO){
                    close(pipeInFd);
                }

                if(!isLast){
                    close(pipeFd[1]);
                    pipeInFd = pipeFd[0];
                }
            }
        }
        if(lastPid > 0){ 
            bool isBackground = tknr.commands.back()->isBackground();
                
            if (isBackground) {
                cerr << "[" << background_pids.size() + 1 << "] ";
                for(pid_t p : pipePids){
                    background_pids.push_back(p);
                    cerr << p << " ";
                }
                cerr << endl;
            } else {
                int status = 0;
                waitpid(lastPid, &status, 0);
                for(pid_t p : pipePids){
                    if(p != lastPid){
                        waitpid(p, NULL, WNOHANG); 
                    }
                }

                if(WIFEXITED(status) && WEXITSTATUS(status) > 1){
                    cerr << "Command failed or not found." << endl;
                }
            }
        }
            
        // 셸의 표준 입출력을 원래대로 복원
        if (dup2(origStdin, STDIN_FILENO) < 0) {
            perror("dup2 restore stdin error");
        }
        if (dup2(origStdout, STDOUT_FILENO) < 0) {
            perror("dup2 restore stdout error");
        }
        close(origStdin);
        close(origStdout);
    }
}