#include <iostream>


#include <gtest/gtest.h>
#include "../src/Server.hpp"

TEST (test1, test1) {
    std::string cmd = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r\n";
    Connection c(2);
    c.read_buffer = cmd;
    ASSERT_EQ(c.getCompleteCommandSize(), cmd.size());
}

TEST(test2, test2) {
    std::string cmd = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r\n";
    std::vector<std::string> tokens = {"ECHO", "Hey"};
    auto result = parseRESP(cmd); 
    ASSERT_EQ(tokens.size(), result.size());
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(tokens[i], result[i]);
    }
}

TEST(test3, test3) {
    std::string cmd = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r";
    Connection c(2);
    c.read_buffer = cmd;
    ASSERT_EQ(0, c.getCompleteCommandSize());
}