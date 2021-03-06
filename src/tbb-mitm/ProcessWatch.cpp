
#include "ProcessWatch.hpp"
#include <onions-common/Log.hpp>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <string.h>
#include <netdb.h>


#ifndef INSTALL_PREFIX
#error CMake has not defined INSTALL_PREFIX!
#endif


pid_t ProcessWatch::launchTor(char** argv)
{
  Log::get().notice("Launching Tor...");
  argv[0] = const_cast<char*>("TorBrowser/Tor/torbin");
  pid_t torP = startProcess(argv);

  Log::get().notice("Tor started with pid " + std::to_string(torP));

  // wait for Tor's control port to be available
  while (!isOpen(9151))
  {
    // https://stackoverflow.com/questions/5278582
    if (waitpid(torP, NULL, WNOHANG) != 0)  // test for existence of Tor
      exit(0);  // if the Tor Browser was closed, then quit
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  Log::get().notice("Tor's control port is now available.");

  return torP;
}



pid_t ProcessWatch::launchOnioNS(pid_t torP)
{
  // start the onions-client executable
  Log::get().notice("Launching OnioNS client software...");
  pid_t ocP =
      ProcessWatch::startProcess(ProcessWatch::getOnionsClientProcess());

  Log::get().notice("OnioNS-client started with pid " + std::to_string(ocP));

  // wait until onions-client has established a connection
  while (!ProcessWatch::isOpen(9053))
  {
    if (waitpid(torP, NULL, WNOHANG) != 0 || waitpid(ocP, NULL, WNOHANG) != 0)
    {
      // kill children
      kill(torP, SIGQUIT);
      kill(ocP, SIGQUIT);

      return EXIT_FAILURE;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  Log::get().notice("onions-client IPC port is now available.");

  return ocP;
}



pid_t ProcessWatch::launchStem()
{
  // launch the Stem script
  Log::get().notice("Launching OnioNS-TBB software...");
  pid_t stemP = ProcessWatch::startProcess(ProcessWatch::getStemProcess());
  Log::get().notice("Stem script started with pid " + std::to_string(stemP));
  return stemP;
}



// start a child process based on the given commands
pid_t ProcessWatch::startProcess(char** args)
{  // http://www.cplusplus.com/forum/lounge/17684/
  pid_t pid = fork();

  switch (pid)
  {
    case -1:
      Log::get().warn("Uh-Oh! fork() failed.");
      exit(1);
    case 0:                   // Child process
      execvp(args[0], args);  // Execute the program
      Log::get().warn("Failed to execute process!");
      exit(1);
    default: /* Parent process */
      Log::get().warn("Process \"" + std::string(args[0]) + "\" created, pid " +
                      std::to_string(pid));
      // wait(&pid);
      return pid;
  }
}



// test if a localhost port is open or not
bool ProcessWatch::isOpen(int port)
{  // https://theredblacktree.wordpress.com/2013/09/30/how-to-check-if-a-port-is-open-or-not-in-c-unixlinux/

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    Log::get().warn("Error opening socket!");
    return false;
  }

  struct hostent* server = gethostbyname("127.0.0.1");
  if (server == NULL)
  {
    Log::get().warn("ERROR, no such host");
    return false;
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy(&server->h_addr, &serv_addr.sin_addr.s_addr,
         static_cast<size_t>(server->h_length));

  serv_addr.sin_port = htons(port);
  bool isOpen = connect(sockfd, reinterpret_cast<struct sockaddr*>(&serv_addr),
                        sizeof(serv_addr)) >= 0;
  close(sockfd);
  return isOpen;
}



bool ProcessWatch::isRunning(pid_t p)
{  // http://www.unix.com/programming/121343-c-wexitstatus-question.html

  errno = 0;
  if (kill(p, 0) == 0)
    return true;
  else
  {
    if (errno == ESRCH)
      Log::get().notice("Child process " + std::to_string(p) + " has exited.");
    else
      Log::get().warn("Could not signal process " + std::to_string(p) + "!");

    return false;
  }
}



// get commmand for launching the onions-client executable
char** ProcessWatch::getOnionsClientProcess()
{
  const char** args = new const char* [4];
  args[0] = "onions-client\0";
  args[1] = "-o\0";
  args[2] = "TorBrowser/OnioNS/client.log\0";
  args[3] = NULL;
  return const_cast<char**>(args);
}



// get command for launching the Stem script
char** ProcessWatch::getStemProcess()
{
  const char** args = new const char* [3];
  args[0] = "python\0";
  args[1] = (INSTALL_PREFIX + "/bin/onions-stem.py").c_str();
  args[2] = NULL;
  return const_cast<char**>(args);
}
