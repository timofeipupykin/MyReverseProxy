#pragma once

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

#include "proxy/backend.hpp"
#include "proxy/error.hpp"

namespace proxy {

// Формат: по одной записи host:port в строке, '#' начинает комментарий.
[[nodiscard]] std::expected<std::vector<BackendPtr>, Error> parse_backends(std::string_view text);

[[nodiscard]] std::expected<std::vector<BackendPtr>, Error> load_backends(
    const std::filesystem::path& file);

}  // namespace proxy
