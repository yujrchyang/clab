#include <gtest/gtest.h>

#include <cstring>

#include "common/buffer_error.h"

using clab::buffer::end_of_buffer;
using clab::buffer::malformed_input;

TEST(BufferErrorTest, EndOfBufferIsException) {
    try {
        throw end_of_buffer();
    } catch (const std::exception &e) {
        SUCCEED();
    }
}

TEST(BufferErrorTest, MalformedInputHasMessage) {
    try {
        throw malformed_input("bad data");
    } catch (const malformed_input &e) {
        EXPECT_STREQ(e.what(), "bad data");
    }
}

TEST(BufferErrorTest, MalformedInputIsException) {
    try {
        throw malformed_input("err");
    } catch (const std::exception &e) {
        SUCCEED();
    }
}

TEST(BufferErrorTest, EndOfBufferIsError) {
    try {
        throw end_of_buffer();
    } catch (const clab::buffer::error &e) {
        SUCCEED();
    }
}

TEST(BufferErrorTest, MalformedInputIsError) {
    try {
        throw malformed_input("err");
    } catch (const clab::buffer::error &e) {
        SUCCEED();
    }
}
