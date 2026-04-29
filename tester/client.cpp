#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <vector>
#include <thread>
#include <climits>


#define BUFFER_SIZE 256

#include "../src/include/Utils.hpp"
#include "../src/include/types.h"

int main(int argc, char **argv) {
    
   int sock = 0;
   struct sockaddr_in serv_addr;
   char buffer[BUFFER_SIZE] = {0};
   int port = 6379;

   if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    std::cerr << "Socket creation error"  << std::endl;
    return -1;
   }

   serv_addr.sin_family = AF_INET;
   serv_addr.sin_port = htons(port);

   if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address" << std::endl;
    return -1;
   }


   if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      std::cerr << "Connection failed" << std::endl;
      return -1;
   }

   auto send_q = [&](const std::string& query) {
      send(sock, query.c_str(), query.size(), 0);
      std::cout << "Message sent" << std::endl;
   };

   auto receive_q = [&]() {
      ssize_t valread = recv(sock, buffer, BUFFER_SIZE, 0);
      // auto resp = parseRESP(std::string(buffer, buffer+valread));
      for (ssize_t i = 0; i < valread; i++) {
         if ((buffer[i] == '\r') || (buffer[i] == '\n')) buffer[i] = ' ';
      }
      std::cout << "Received: " << valread << " bytes." << std::endl;
      std::cout << std::string(buffer, buffer+valread) << std::endl;
      // if (!resp.empty()) std::cout << resp[0] << std::endl;
      std::memset(buffer, 0, BUFFER_SIZE);
      if (valread == 0) return;
   };
   
   auto encode = [](const std::vector<std::string>& args) {
      return RESP::encodeSequence(args.begin(), args.end());
   };

   auto send_command = [&](const std::vector<std::string>& args) {
      std::string q_q = RESP::encodeSequence(args.begin(), args.end());
      send_q(q_q);
      receive_q(); 
   };

   {
      std::vector<std::string> tokens = {"MULTI"};
      send_command(tokens);

      tokens = {"SET" , "grape" , "66"};
      send_command(tokens);
      tokens = {"INCR", "grape"};
      send_command(tokens);
      tokens = {"INCR", "blueberry"};
      send_command(tokens);
      tokens = {"EXEC"};
      send_command(tokens);
    
   }
   close(sock);
   return 0;
}