#include "proxy/config.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <string>

#include "proxy/string_utils.hpp"

namespace proxy {

std::expected<std::vector<BackendPtr>, Error> parse_backends(std::string_view text) {
    std::vector<BackendPtr> result;

    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = (nl == std::string_view::npos) ? std::string_view{} : text.substr(nl + 1);

        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = detail::trim(line);
        if (line.empty()) continue;

        const auto colon = line.rfind(':');
        if (colon == std::string_view::npos || colon == 0) {
            return std::unexpected(Error::InvalidConfig);
        }

        const std::string_view host = line.substr(0, colon);
        const std::string_view port_str = line.substr(colon + 1);

        unsigned port = 0;
        const auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
        if (ec != std::errc{} || ptr != port_str.data() + port_str.size() || port == 0 || port > 65535) {
            return std::unexpected(Error::InvalidConfig);
        }

        result.push_back(std::make_shared<Backend>(std::string(host), static_cast<std::uint16_t>(port)));
    }

    if (result.empty()) return std::unexpected(Error::InvalidConfig);
    return result;
}

std::expected<std::vector<BackendPtr>, Error> load_backends(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return std::unexpected(Error::InvalidConfig);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parse_backends(text);
}

}  // namespace proxy
