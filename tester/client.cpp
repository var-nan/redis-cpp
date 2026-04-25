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


#define BUFFER_SIZE 256

#include "../src/Utils.hpp"
#include "../src/types.h"

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
      std::cout << "Received: " << valread << " bytes." << std::endl;
      std::cout << std::string(buffer, buffer+valread) << std::endl;
      // if (!resp.empty()) std::cout << resp[0] << std::endl;
      std::memset(buffer, 0, BUFFER_SIZE);
      if (valread == 0) return;
   };

   {
      for (int i = 0; i < 1; i++) {
         std::vector<std::string> tokens = {"Rpush", "Foo", "Car", "Nand", "Gate"};
         std::string set_q = RESP::encodeSequence(tokens.begin(), tokens.end());
         send_q(set_q);
         receive_q();
         // thread sleep?
         // std::this_thread::sleep_for(std::chrono::milliseconds(3000));

         std::vector<std::string> tokens2 = {"LRange", "Foo", "-3", "-2"};
         std::string get_q = RESP::encodeSequence(tokens2.begin(), tokens2.end());
         send_q(get_q);
         receive_q();
      }
   }

   for (int i = 0; i < 6 && false; i++) {
      std::string hello  = "*2\r\n$4\r\nECHO\r\n$3\r\nHe" + std::to_string(i)+"\r\n";
      send(sock, hello.c_str(), hello.size(), 0);
      std::cout << "message sent" << std::endl;
      ssize_t valread = read(sock, buffer, BUFFER_SIZE);
      std::cout << "Received: " << valread << " bytes : " << buffer << std::endl;
      std::memset(buffer, 0, BUFFER_SIZE);
   }
   close(sock);
   return 0;
}