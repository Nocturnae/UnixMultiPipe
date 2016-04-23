#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <algorithm>

extern "C" {
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
}

using namespace std;

// AUXILIARY FUNCTIONS

template <typename T>
void printVector(vector<T> v) {
    for (int i = 0; i < v.size(); i++)
        cout << v[i] << " ";
    cout << endl;
}

// DATA STRUCTURES

class process {
public:
    int processID;
    string command;
    vector<string> args;
};

class pipes {
public:
    int pipeID;
    int* fds;
};

vector<process> processVector;
vector<pipes> pipeVector;
map<int, int> out; // processID -> pipeID
map<int, vector<int> > in; // processID -> vector<pipeID>
vector<int> pin; // pipeID to read from
vector<int> pout; // pipeID to write to

// HELPER FUNCTIONS

bool inVector(vector<int> v, int elem) {
    bool flag = false;
    for (int i = 0; i < v.size(); i++) {
        if (v[i] == elem) {
            flag = true;
            break;
        }
    }
    return flag;
}

bool graphComplete() { // returns true if graph is complete
    bool flag = false;
    if ((pin.size() == pout.size()) && (pin.size() != 0) && (equal(pin.begin(), pin.end(), pout.begin())))
        flag = true;
    return flag;
}

// MAIN

int main(int argc, const char * argv[]) {
    
    string line;
    int id = 0;
    vector<pid_t> pids;
    bool reset = false;
    
    while (getline(cin, line)) {
        if (line.compare("quit") == 0)
            break;
        else {
            // parse input
            istringstream ss(line);
            process p;
            if (ss >> p.command) {
                p.processID = id++;
                string arg;
                int pipeNo;
                while (ss >> arg) {
                    if (arg.compare("<|") == 0) {
                        ss >> pipeNo;
                        out[p.processID] = pipeNo;
                        if (!inVector(pout, pipeNo))
                            pout.push_back(pipeNo);
                    }
                    else if (arg.compare(">|") == 0) {
                        while (ss >> pipeNo) {
                            in[p.processID].push_back(pipeNo);
                            if (!inVector(pin, pipeNo))
                                pin.push_back(pipeNo);
                        }
                    }
                    else
                        p.args.push_back(arg);
                }
                processVector.push_back(p);
            }
            
            sort(pin.begin(), pin.end());
            sort(pout.begin(), pout.end());
            
            if (graphComplete() || ((pin.size() == 0) && (pout.size() == 0))) {
                
                // CREATE PIPES
                for (int i = 0; i < pin.size(); i++) {
                    pipes p;
                    p.pipeID = pin[i];
                    p.fds = new int[2];
                    pipeVector.push_back(p);
                }
                
                unsigned long pipeNum = pipeVector.size(); // 1
                
                for (int i = 0; i < pipeNum; i++) {
                    if (pipe(pipeVector[i].fds) < 0)
                        perror("pipe failed\n");
                }
                
                // EXECUTE PROCESSES
                unsigned long processNum = processVector.size(); // 2
                for (int i = 0; i < processNum; i++) {
                    process curProcess = processVector[i];
                    bool replaceStdin = false; pipes pipeIn;
                    bool replaceStdout = false; vector<pipes> pipeOut;
                    
                    // find pipe to replace stdin
                    if (out.find(curProcess.processID) != out.end()) {
                        replaceStdin = true;
                        int pipeInID = out[curProcess.processID];
                        for (int j = 0; j < pipeNum; j++) {
                            if (pipeVector[j].pipeID == pipeInID) {
                                pipeIn = pipeVector[j];
                                break;
                            }
                        }
                    }
                    
                    // find pipe to replace stdout
                    if (in.find(curProcess.processID) != in.end()) {
                        replaceStdout = true;
                        vector<int> pipeOutID = in[curProcess.processID];
                        for (int k = 0; k < pipeOutID.size(); k++) {
                            for (int j = 0; j < pipeNum; j++) {
                                if (pipeVector[j].pipeID == pipeOutID[k]) {
                                    pipeOut.push_back(pipeVector[j]);
                                    break;
                                }
                            }
                        }
                    }
                    
                    // configure executable
                    unsigned long argSize = curProcess.args.size();
                    char* argv[argSize + 2];
                    
                    argv[0] = new char[strlen(curProcess.command.c_str()) + 1];
                    strcpy(argv[0], curProcess.command.c_str());
                    
                    for (int k = 0; k < argSize; k++) {
                        argv[k + 1] = new char[strlen(curProcess.args[k].c_str()) + 1];
                        strcpy(argv[k + 1], curProcess.args[k].c_str());
                    }
                    
                    argv[argSize + 1] = NULL;
                    
                    // execute
                    
                    pid_t pid = fork();
                    pids.push_back(pid);
                    
                    if (pid == 0) {
                        
                        if (!replaceStdin && !replaceStdout) { // check
                            
                            for (int k = 0; k < pipeNum; k++) {
                                close(pipeVector[k].fds[0]);
                                close(pipeVector[k].fds[1]);
                            }
                            
                            execvp(argv[0], argv);
                            perror("noooo0");
                            
                        }
                     
                        else if (replaceStdin && !replaceStdout) { // replace only stdin
                            
                            for (int k = 0; k < pipeNum; k++) {
                                if (pipeVector[k].pipeID != pipeIn.pipeID)
                                    close(pipeVector[k].fds[0]);
                                close(pipeVector[k].fds[1]);
                            }
                            
                            dup2(pipeIn.fds[0], fileno(stdin));
                            close(pipeIn.fds[0]);
                            
                            execvp(argv[0], argv);
                            perror("noooo1");
                            
                        }
                        
                        else if (!replaceStdin && replaceStdout) { // replace only stdout, e.g: man
                            
                            if (pipeOut.size() > 1) { // requires repeater
                                
                                pipes rep; // repeater pipe
                                rep.pipeID = -1;
                                rep.fds = new int[2];
                                
                                pipe(rep.fds);
                                
                                pid_t pid2 = fork();
                                
                                if (pid2 > 0) { // repeater process
                                    
                                    char buffer[1000];
                                    int status;
                                    size_t bytes;
                                    //wait(&status);
                                    
                                    for (int l = 0; l < pipeOut.size(); l++) {
                                        close(pipeOut[l].fds[0]);
                                    }
                                    
                                    close(rep.fds[1]);
                                    dup2(rep.fds[0], fileno(stdin));
                                    
                                    while ((bytes = read(rep.fds[0], buffer, 1000)) > 0) { // read chunk
                                        for (int k = 0; k < pipeOut.size(); k++) {
                                            write(pipeOut[k].fds[1], buffer, bytes);
                                        }
                                    }
                                    
                                    close(rep.fds[0]);
                                    
                                    for (int k = 0; k < pipeNum; k++) {
                                        close(pipeVector[k].fds[0]);
                                        close(pipeVector[k].fds[1]);
                                    }
                                    
                                    exit(0);
                                }
                                
                                else { // actual process
                                    
                                    for (int k = 0; k < pipeNum; k++) {
                                        close(pipeVector[k].fds[0]);
                                        close(pipeVector[k].fds[1]);
                                    }
                                    
                                    close(rep.fds[0]);
                                    dup2(rep.fds[1], fileno(stdout));
                                    close(rep.fds[1]);
                                    
                                    execvp(argv[0], argv);
                                    perror("noooo2");
                                }
                            }
                            
                            else {
                                
                                for (int k = 0; k < pipeNum; k++) {
                                    close(pipeVector[k].fds[0]);
                                    for (int l = 0; l < pipeOut.size(); l++)
                                        if (pipeOut[l].pipeID != pipeVector[k].pipeID)
                                            close(pipeVector[k].fds[1]);
                                }
                                
                                for (int l = 0; l < pipeOut.size(); l++)
                                    dup2(pipeOut[l].fds[1], fileno(stdout));
                                
                                for (int l = 0; l < pipeOut.size(); l++)
                                    close(pipeOut[l].fds[1]);
                                
                                execvp(argv[0], argv);
                                perror("noooo2");
                                
                            }
                        }
                        
                        else if (replaceStdin && replaceStdout) { // replace both stdin & stdout
                            
                            if (pipeOut.size() > 1) { // requires repeater
                                
                                pipes rep; // repeater pipe
                                rep.pipeID = -1;
                                rep.fds = new int[2];
                                
                                pipe(rep.fds);
                                
                                pid_t pid2 = fork();
                                
                                if (pid2 > 0) { // repeater process
                                    
                                    char buffer[1000];
                                    int status;
                                    size_t bytes;
                                    //wait(&status);
                                    
                                    close(pipeIn.fds[0]);
                                    close(pipeIn.fds[1]);
                                    
                                    for (int l = 0; l < pipeOut.size(); l++) {
                                        close(pipeOut[l].fds[0]);
                                    }
                                    
                                    close(rep.fds[1]);
                                    dup2(rep.fds[0], fileno(stdin));
                                    
                                    while ((bytes = read(rep.fds[0], buffer, 1000)) > 0) { // read chunk
                                        for (int k = 0; k < pipeOut.size(); k++) {
                                            write(pipeOut[k].fds[1], buffer, bytes);
                                        }
                                    }
                                    
                                    close(rep.fds[0]);
                                    
                                    for (int k = 0; k < pipeNum; k++) {
                                        close(pipeVector[k].fds[0]);
                                        close(pipeVector[k].fds[1]);
                                    }
                                    
                                    exit(0);
                                }

                                
                                else { // actual process
                                    
                                    for (int k = 0; k < pipeNum; k++) {
                                        if (pipeVector[k].pipeID != pipeIn.pipeID)
                                            close(pipeVector[k].fds[0]);
                                        close(pipeVector[k].fds[1]);
                                    }
                                    
                                    dup2(pipeIn.fds[0], fileno(stdin));
                                    close(pipeIn.fds[0]);
                                    
                                    close(rep.fds[0]);
                                    dup2(rep.fds[1], fileno(stdout));
                                    close(rep.fds[1]);
                                    
                                    execvp(argv[0], argv);
                                    perror("noooo2");
                                    
                                }
                            }
                            
                            else {
                                
                                for (int k = 0; k < pipeNum; k++) {
                                    if (pipeVector[k].pipeID != pipeIn.pipeID)
                                        close(pipeVector[k].fds[0]);
                                    for (int l = 0; l < pipeOut.size(); l++)
                                        if (pipeOut[l].pipeID != pipeVector[k].pipeID)
                                            close(pipeVector[k].fds[1]);
                                }
                                
                                dup2(pipeIn.fds[0], fileno(stdin));
                                close(pipeIn.fds[0]);
                                
                                for (int l = 0; l < pipeOut.size(); l++)
                                    dup2(pipeOut[l].fds[1], fileno(stdout));
                                
                                for (int l = 0; l < pipeOut.size(); l++)
                                    close(pipeOut[l].fds[1]);
                                
                                execvp(argv[0], argv);
                                perror("noooo3");
                            }
                            
                        }
                    }
                    
                    
                }
                // parent
                for (int i = 0; i < pipeNum; i++) {
                    close(pipeVector[i].fds[0]);
                    close(pipeVector[i].fds[1]);
                }
                for (int i = 0; i < pids.size(); i++) {
                    wait(&pids[i]);
                }
                reset = true;
            }
            
            if (reset) {
                // prepare for next graph
                pipeVector.clear();
                processVector.clear();
                pout.clear();
                pin.clear();
                out.clear();
                in.clear();
                reset = false;}
            }
        }
    
    return 0;
}

