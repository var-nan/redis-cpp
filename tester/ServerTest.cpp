#include <iostream>


#include <gtest/gtest.h>
#include "Server.hpp"

TEST (test1, test1) {
    std::string cmd = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r\n";
    Connection c(2);
    c.read_buffer = cmd;
    ASSERT_EQ(c.getCompleteCommandSize(), cmd.size());
}

TEST(test2, test2) {
    std::string cmd = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r\n";
    cmd = "*2\r\n$3\r\ngeT\r\n$3\r\nFoo\r\n";
    cmd = "*3\r\n$3\r\nSET\r\n$3\r\nFoo\r\n$3\r\nbar\r\n";
    std::vector<std::string> tokens = {"SET", "Foo", "bar"};
    auto result = parseRESP(cmd); 
    ASSERT_EQ(tokens.size(), result.size());
    for (int i = 0; i < tokens.size(); i++) {
        ASSERT_EQ(tokens[i], result[i]);
    }
}

TEST(test3, test3) {
    std::string cmd = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r";
    Connection c(2);
    c.read_buffer = cmd;
    ASSERT_EQ(0, c.getCompleteCommandSize());
}

TEST(test4, test4) {
    std::string cmd2 = "*3\r\n$3\r\nSET\r\n$3\r\nFoo\r\n$3\r\nbar\r\n";
    std::string cmd3 = "*2\r\n$4\r\nECHO\r\n$3\r\nHey\r\n";
    std::string cmd = cmd2 + cmd3;

    Connection c(3);
    c.read_buffer = cmd;

    size_t cmd_size = c.getCompleteCommandSize();
    ASSERT_EQ(cmd2.size(), cmd_size);

    std::vector<std::string> tokens = {"SET", "Foo", "bar"};
    auto result = parseRESP(c.read_buffer.substr(0, cmd_size)); 
    ASSERT_EQ(tokens.size(), result.size());
    for (int i = 0; i < tokens.size(); i++) {
        ASSERT_EQ(tokens[i], result[i]);
    }
    // erase first part
    c.read_buffer.erase(0, cmd_size);
    ASSERT_EQ(c.read_buffer.size(), cmd3.size());
    cmd_size = c.getCompleteCommandSize();
    ASSERT_EQ(cmd3.size(), cmd_size);
    tokens = {"ECHO", "Hey"};
    result = parseRESP(c.read_buffer.substr(0, cmd_size));
    ASSERT_EQ(tokens.size(), result.size());
    for (int i = 0; i < tokens.size(); i++){
        ASSERT_EQ(tokens[i], result[i]);
    }
}