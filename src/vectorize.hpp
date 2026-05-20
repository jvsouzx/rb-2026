#ifndef VECTORIZE_HPP
#define VECTORIZE_HPP

#include <array>
#include <string_view>

std::array<float, 14> vectorizeTransaction(std::string_view body);

#endif
