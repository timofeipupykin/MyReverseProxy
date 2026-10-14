#include "proxy/http_parser.hpp"

#include <algorithm>
#include <cctype>

#include "proxy/string_utils.hpp"

namespace proxy {

namespace {

bool iequals(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}

}  // namespace

std::optional<std::string_view> HttpRequestHead::find_header(std::string_view name) const {
    for (const auto& h : headers) {
        if (iequals(h.name, name)) return h.value;
    }
    return std::nullopt;
}

std::expected<HttpRequestHead, Error> parse_request_head(std::string_view data) {
    constexpr std::string_view kCrlf = "\r\n";
    constexpr std::string_view kEnd = "\r\n\r\n";

    const auto end = data.find(kEnd);
    if (end == std::string_view::npos) {
        return std::unexpected(data.size() > kMaxHeadSize ? Error::HeaderTooLarge
                                                          : Error::IncompleteRequest);
    }
    if (end + kEnd.size() > kMaxHeadSize) return std::unexpected(Error::HeaderTooLarge);

    HttpRequestHead head;
    head.head_size = end + kEnd.size();

    const std::string_view block = data.substr(0, end);
    const auto first_line_end = block.find(kCrlf);
    const std::string_view request_line = block.substr(0, first_line_end);

    // METHOD SP TARGET SP VERSION
    const auto sp1 = request_line.find(' ');
    if (sp1 == std::string_view::npos) return std::unexpected(Error::MalformedRequestLine);
    const auto sp2 = request_line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return std::unexpected(Error::MalformedRequestLine);

    head.method = request_line.substr(0, sp1);
    head.target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    head.version = request_line.substr(sp2 + 1);

    if (head.method.empty() || head.target.empty() || !head.version.starts_with("HTTP/")) {
        return std::unexpected(Error::MalformedRequestLine);
    }

    std::string_view rest =
        first_line_end == std::string_view::npos ? std::string_view{} : block.substr(first_line_end + 2);

    while (!rest.empty()) {
        const auto e = rest.find(kCrlf);
        const std::string_view line = rest.substr(0, e);
        rest = (e == std::string_view::npos) ? std::string_view{} : rest.substr(e + 2);

        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return std::unexpected(Error::MalformedHeader);
        }
        head.headers.push_back({line.substr(0, colon), detail::trim(line.substr(colon + 1))});
    }

    return head;
}

}  // namespace proxy
