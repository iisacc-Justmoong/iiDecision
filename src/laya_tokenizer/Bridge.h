#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
void* iid_laya_open(const char* path, char** error);
void iid_laya_close(void* handle);
void iid_laya_config(const void* handle, std::size_t* budgets, std::uint32_t* special);
double iid_laya_temperature(const void* handle, std::uint32_t qtype, std::size_t count);
int iid_laya_encode(const void* handle, const unsigned char* text, std::size_t length,
    std::uint32_t** ids, std::size_t* count, char** error);
void iid_laya_tokens_free(std::uint32_t* ids, std::size_t count);
void iid_laya_error_free(char* error);
}
