#include <iostream>
#include <vector>
#include <string>
#include <unistd.h> // fork, execvp, pipe, dup2, close, chdir, getcwd
#include <sys/wait.h> // waitpid
#include <sys/types.h> // pid_t
#include <sys/stat.h> // S_IRUSR, S_IWUSR, etc. for open
#include <fcntl.h> // open, O_RDONLY, O_WRONLY, etc.
#include <limits.h> // PATH_MAX
#include <time.h> // time, ctime
#include <errno.h> // errno
#include <cstdlib> // getenv, exit
#include <algorithm> // distance

#include "Tokenizer.h"

// Color definitions
#define RED     "\033[1;31m"
#define GREEN	"\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE	"\033[1;34m"
#define WHITE	"\033[1;37m"
#define NC      "\033[0m" // No Color

using namespace std;

string previousDir = "";  // For "cd -"
vector<pid_t> background_pids; // For reaping background processes

// Helper function to convert vector<string> to vector<char*> for execvp
vector<char*> prepare_exec_args(vector<string>& args) {
    vector<char*> cArgs;
    for (auto& s : args) {
        cArgs.push_back((char*)(s.c_str()));
    }
    cArgs.push_back(nullptr); // execvp requires a NULL-terminated array
    return cArgs;
}

int main () {
    // Get initial previousDir from PWD environment variable if available
    const char* initial_pwd = getenv("PWD");
    if (initial_pwd != nullptr) {
        previousDir = string(initial_pwd);
    } else {
        // Fallback if PWD is not set, try getcwd
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
             previousDir = string(cwd);
        }
    }


    for (;;) {
        // Reap completed background processes non-blockingly
        for (auto it = background_pids.begin(); it != background_pids.end();) {
            int status;
            pid_t result = waitpid(*it, &status, WNOHANG);
            if (result == 0) { // Still running
                ++it;
            } else if (result == -1) { // Error (or already reaped)
                if (errno != ECHILD) { // Ignore "No child processes" error
                    perror("waitpid error reaping background process"); // 오류 메시지 좀 더 명확하게
                }
                it = background_pids.erase(it); // Remove from list regardless
            } else { // Completed
                // 백그라운드 완료 메시지 개선 (프롬프트 덮어쓰기 방지 위해 \n 추가)
                cerr << GREEN << "\n[" << (distance(background_pids.begin(), it) + 1) << "] Done: " << *it << NC << endl;
                it = background_pids.erase(it);
            }
        }

        // --- Print Prompt ---
        char cwd[PATH_MAX];
        string currentPath = (getcwd(cwd, sizeof(cwd)) != NULL) ? string(cwd) : "getcwd_error";

        const char* user = getenv("USER");
        string username = (user != nullptr) ? string(user) : "unknown";

        time_t rawtime;
        time(&rawtime);
        string timeStr = string(ctime(&rawtime));
        if (!timeStr.empty() && timeStr.back() == '\n') {
            timeStr.pop_back(); // Remove trailing newline from ctime
        }

        cout << GREEN << timeStr << " "
             << username << ":"
             << currentPath << NC
             << YELLOW << "$ " << NC << flush; // Use flush to ensure prompt appears immediately

        // --- Read Input ---
        string input;
        if (!getline(cin, input)) { // Check for getline failure (e.g., EOF)
            if (cin.eof()) {
                cout << endl; // Print newline after Ctrl+D
                input = "exit"; // Treat EOF as exit command
            } else {
                 perror("getline error");
                 continue; // Try again on non-EOF error
            }
        }

        // Exit command
        if (input == "exit") {
            cout << RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            // Optionally, kill remaining background processes before exiting?
            break;
        }

        // Skip empty input
        if (input.empty() || input.find_first_not_of(" \t\n\r") == string::npos) {
            continue;
        }

        // --- Tokenize Input ---
        Tokenizer tknr(input);
        if (tknr.hasError() || tknr.commands.empty()) {
            continue; // Skip if tokenizer failed or produced no commands
        }


        // --- Handle Built-in 'cd' ---
        // 'cd' must run in the parent process and cannot be part of a pipeline
        if (tknr.commands.at(0)->args.at(0) == "cd") {
            if (tknr.commands.size() > 1) {
                cerr << "Error: 'cd' command cannot be used in a pipeline." << endl;
                continue;
            }
             // Check if 'cd' itself is marked as background (e.g., 'cd .. &') - doesn't make sense
            if (tknr.commands.at(0)->isBackground()){
                 cerr << "Error: 'cd' command cannot run in background." << endl;
                 continue;
            }

            vector<string>& args = tknr.commands.at(0)->args;
            string targetPath;
            char oldCwd[PATH_MAX];
             // Store current directory *before* changing
            string currentDir = (getcwd(oldCwd, sizeof(oldCwd)) != NULL) ? string(oldCwd) : "";

            if (args.size() < 2) { // cd to HOME
                const char* homeDir = getenv("HOME");
                if (homeDir == NULL) {
                    cerr << "cd: HOME not set" << endl;
                    continue;
                }
                targetPath = homeDir;
            } else if (args.at(1) == "-") { // cd to previous directory
                if (previousDir.empty()) { // Check if previousDir was ever set
                    cerr << "cd: OLDPWD not set" << endl; // bash 오류 메시지와 유사하게 변경
                    continue;
                }
                targetPath = previousDir;
            } else { // cd to specified path
                targetPath = args.at(1);
            }

            if (chdir(targetPath.c_str()) != 0) {
                perror(("cd: " + targetPath).c_str()); // More informative error
            } else { // Success
                 if (!currentDir.empty()) { // Only update previousDir if getcwd worked before chdir
                     previousDir = currentDir;
                 }
                 // Print new directory only for 'cd -'
                if (args.size() >= 2 && args.at(1) == "-") {
                    char finalCwd[PATH_MAX];
                     if (getcwd(finalCwd, sizeof(finalCwd)) != NULL) {
                        cout << finalCwd << endl;
                     } else {
                        perror("getcwd after cd - error");
                     }
                }
            }
            continue; // Finished 'cd', continue to next prompt
        }

        // --- Execute External Commands (Pipeline) ---

        // Backup standard I/O file descriptors
        int origStdin = dup(STDIN_FILENO);
        if (origStdin == -1) { perror("dup origStdin error"); continue; }
        int origStdout = dup(STDOUT_FILENO);
        if (origStdout == -1) { perror("dup origStdout error"); close(origStdin); continue; }

        int numCommands = tknr.commands.size();
        int pipeInFd = STDIN_FILENO; // Input for the *next* command in the pipe
        pid_t lastPid = -1; // PID of the last command in the pipeline
        vector<pid_t> pipePids; // PIDs of all children in this pipeline
        bool pipeline_setup_error = false;

        for (int i = 0; i < numCommands; ++i) {
            Command* currCmd = tknr.commands.at(i);
            bool isLastCommandInPipeline = (i == numCommands - 1);

            // Create pipe for communication between this child and the next (if not last)
            int pipeFd[2] = {-1, -1};
            if (!isLastCommandInPipeline) {
                if (pipe(pipeFd) == -1) {
                    perror("pipe error");
                    pipeline_setup_error = true;
                    break;
                }
            }

            pid_t pid = fork();
            if (pid < 0) { // Fork failed
                perror("fork error");
                if (!isLastCommandInPipeline) { close(pipeFd[0]); close(pipeFd[1]); } // Close pipe fds on error
                pipeline_setup_error = true;
                break;
            }

            // --- Child Process ---
            if (pid == 0) {
                // Redirect Input (< file or pipeInFd)
                if (currCmd->hasInput()) {
                    // Input redirection is only allowed for the first command per PA description
                    if (i != 0) {
                         cerr << "Error: Input redirection ('<') only allowed for the first command in a pipeline." << endl;
                         exit(EXIT_FAILURE);
                    }
                    int fdIn = open(currCmd->in_file.c_str(), O_RDONLY);
                    if (fdIn < 0) { perror(("open input error: " + currCmd->in_file).c_str()); exit(EXIT_FAILURE); }
                    if (dup2(fdIn, STDIN_FILENO) < 0) { perror("dup2 input error"); exit(EXIT_FAILURE); }
                    close(fdIn);
                } else if (pipeInFd != STDIN_FILENO) { // Input comes from previous pipe
                    if (dup2(pipeInFd, STDIN_FILENO) < 0) { perror("dup2 pipe input error"); exit(EXIT_FAILURE); }
                    close(pipeInFd); // Close the read end we just duplicated
                }
                // (else: use original STDIN_FILENO for the first command if no '<')

                // Redirect Output (> file or pipeFd[1])
                if (currCmd->hasOutput()) {
                    // Output redirection is only allowed for the last command per PA description
                    if (!isLastCommandInPipeline) {
                         cerr << "Error: Output redirection ('>') only allowed for the last command in a pipeline." << endl;
                         exit(EXIT_FAILURE);
                    }
                    int fdOut = open(currCmd->out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // 0644
                    if (fdOut < 0) { perror(("open output error: " + currCmd->out_file).c_str()); exit(EXIT_FAILURE); }
                    if (dup2(fdOut, STDOUT_FILENO) < 0) { perror("dup2 output error"); exit(EXIT_FAILURE); }
                    close(fdOut);
                } else if (!isLastCommandInPipeline) { // Output goes to next pipe
                    if (dup2(pipeFd[1], STDOUT_FILENO) < 0) { perror("dup2 pipe output error"); exit(EXIT_FAILURE); }
                    // Child doesn't need pipe FDs after dup2
                    close(pipeFd[0]);
                    close(pipeFd[1]);
                }
                // (else: use original STDOUT_FILENO for the last command if no '>')

                 // Prepare arguments for execvp - *** Important: Use CURRENT command's args ***
                 vector<char*> cArgs = prepare_exec_args(currCmd->args);


                // Execute command
                execvp(cArgs[0], cArgs.data());

                // execvp only returns on error
                perror(cArgs[0]);
                exit(EXIT_FAILURE); // Use EXIT_FAILURE for execvp errors (more conventional than 2 or 127 sometimes)
            }
            // --- Parent Process ---
            else {
                pipePids.push_back(pid);
                if (isLastCommandInPipeline) {
                    lastPid = pid;
                }

                // Close the read end of the *previous* pipe (parent doesn't need it anymore)
                if (pipeInFd != STDIN_FILENO) {
                    close(pipeInFd);
                }
                // Close the write end of the *current* pipe (parent doesn't need it)
                if (!isLastCommandInPipeline) {
                    close(pipeFd[1]);
                    // Save the read end of the current pipe for the next child
                    pipeInFd = pipeFd[0];
                }
            }
        } // End of pipeline for loop

        // --- Wait for Pipeline Completion (or handle background) ---

        // Clean up last pipe's read end if setup failed mid-way
        if (pipeline_setup_error && pipeInFd != STDIN_FILENO) {
             close(pipeInFd);
        }
        // Reap any children created before the error
        for(pid_t p : pipePids){
            if (pipeline_setup_error) waitpid(p, NULL, WNOHANG); // Non-blocking attempt
            // else: wait logic handled below
        }


        if (!pipeline_setup_error && lastPid > 0) { // Only wait if pipeline setup succeeded and commands ran
            // Check if the *last* command object indicates background
            bool runInBackground = tknr.commands.back()->isBackground();

            if (runInBackground) {
                // Add all PIDs from this pipeline to the background list
                cerr << "[" << background_pids.size() + 1 << "] ";
                for(size_t i = 0; i < pipePids.size(); ++i){
                     background_pids.push_back(pipePids[i]);
                     cerr << pipePids[i] << (i == pipePids.size() - 1 ? "" : " ");
                }
                cerr << endl;
            } else {
                // Wait for the *last* command to finish
                int status = 0;
                 pid_t waited_pid = waitpid(lastPid, &status, 0);
                 if (waited_pid < 0 && errno != ECHILD) { // Handle waitpid error (ignore ECHILD)
                      perror("waitpid error waiting for foreground pipeline");
                 }


                // Optionally check status for errors (child already printed perror)
                 if (WIFSIGNALED(status)) {
                    cerr << "Command terminated by signal " << WTERMSIG(status) << endl;
                 }


                // Reap other children in the pipeline (they should be finished or finishing)
                for(pid_t p : pipePids){
                    if(p != lastPid){
                        // Use blocking wait here? Or non-blocking?
                        // Non-blocking is safer if a preceding command hangs, but might leave zombies temporarily.
                        // Let's stick to non-blocking for reaping intermediate children.
                        waitpid(p, NULL, WNOHANG);
                    }
                }
            }
        }

        // --- Restore Standard I/O ---
        // Restore even if there were errors to ensure shell continues cleanly
        if (dup2(origStdin, STDIN_FILENO) < 0) { perror("dup2 restore stdin error"); }
        if (dup2(origStdout, STDOUT_FILENO) < 0) { perror("dup2 restore stdout error"); }
        close(origStdin);
        close(origStdout);

    } // End of main shell loop (for (;;))

    return 0; // Should only be reached if 'exit' command breaks the loop
}