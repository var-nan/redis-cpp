#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BUFFER_SIZE 256

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

   for (int i = 0; i < 3; i++) {
      std::string hello  = "*1\r\n$4\r\nPING\r\n";
      send(sock, hello.c_str(), hello.size(), 0);
      std::cout << "message sent" << std::endl;
      ssize_t valread = read(sock, buffer, BUFFER_SIZE);
      std::cout << "Received: " << buffer << std::endl;
   }
   close(sock);
   return 0;
}