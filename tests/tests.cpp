// Минималистичные тесты без внешних зависимостей (assert отключается в Release, поэтому свой CHECK).
#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "proxy/config.hpp"
#include "proxy/http_parser.hpp"
#include "proxy/load_balancer.hpp"
#include "proxy/routing_table.hpp"

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";   \
            ++g_failed;                                                               \
        }                                                                             \
    } while (0)

using namespace proxy;

void test_parser_ok() {
    const std::string req =
        "GET /index.html?x=1 HTTP/1.1\r\nHost: example.com\r\nX-Test:   spaced  \r\n\r\nBODY";
    auto head = parse_request_head(std::string_view{req});
    CHECK(head.has_value());
    CHECK(head->method == "GET");
    CHECK(head->target == "/index.html?x=1");
    CHECK(head->version == "HTTP/1.1");
    CHECK(head->headers.size() == 2);
    CHECK(head->find_header("host") == "example.com");
    CHECK(head->find_header("X-TEST") == "spaced");
    CHECK(!head->find_header("missing").has_value());
    CHECK(req.substr(head->head_size) == "BODY");
    // zero-copy: view указывает внутрь исходной строки
    CHECK(head->method.data() == req.data());
}

void test_parser_errors() {
    CHECK(parse_request_head(std::string_view{"GET / HTTP/1.1\r\nHost: a"}).error() == Error::IncompleteRequest);
    CHECK(parse_request_head(std::string_view{"GARBAGE\r\n\r\n"}).error() == Error::MalformedRequestLine);
    CHECK(parse_request_head(std::string_view{"GET / FTP/1\r\n\r\n"}).error() == Error::MalformedRequestLine);
    CHECK(parse_request_head(std::string_view{"GET / HTTP/1.1\r\nNoColon\r\n\r\n"}).error() == Error::MalformedHeader);
    const std::string huge = "GET / HTTP/1.1\r\n" + std::string(kMaxHeadSize + 10, 'a');
    CHECK(parse_request_head(std::string_view{huge}).error() == Error::HeaderTooLarge);
}

void test_parser_concept_overload() {
    const std::string s = "POST /a HTTP/1.0\r\n\r\n";
    std::vector<std::byte> bytes(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) bytes[i] = static_cast<std::byte>(s[i]);
    auto head = parse_request_head(std::span<const std::byte>(bytes));
    CHECK(head.has_value() && head->method == "POST");
    auto head2 = parse_request_head(bytes);  // std::vector<std::byte>
    CHECK(head2.has_value() && head2->target == "/a");
}

RoutingTable::BackendList make(std::initializer_list<std::uint16_t> ports) {
    RoutingTable::BackendList v;
    for (auto p : ports) v.push_back(std::make_shared<Backend>("127.0.0.1", p));
    return v;
}

void test_round_robin() {
    auto b = make({1, 2, 3});
    RoundRobin rr;
    CHECK(rr.pick(b)->port == 1);
    CHECK(rr.pick(b)->port == 2);
    CHECK(rr.pick(b)->port == 3);
    CHECK(rr.pick(b)->port == 1);

    b[1]->healthy = false;  // пропускаем мёртвых
    for (int i = 0; i < 10; ++i) CHECK(rr.pick(b)->port != 2);

    for (auto& x : b) x->healthy = false;
    CHECK(rr.pick(b) == nullptr);
    CHECK(rr.pick(BackendSpan{}) == nullptr);
}

void test_least_connections() {
    auto b = make({1, 2, 3});
    b[0]->active_connections = 5;
    b[1]->active_connections = 1;
    b[2]->active_connections = 3;
    LeastConnections lc;
    CHECK(lc.pick(b)->port == 2);
    b[1]->healthy = false;
    CHECK(lc.pick(b)->port == 3);
    {
        ConnectionGuard g(b[2]);
        CHECK(b[2]->active_connections == 4);
    }
    CHECK(b[2]->active_connections == 3);
}

void test_routing_table() {
    RoutingTable t;
    Balancer rr(Algorithm::RoundRobin);
    CHECK(t.select(rr).error() == Error::NoHealthyBackend);

    auto first = make({1, 2});
    auto keep = first[0];
    keep->active_connections = 7;
    t.update(std::move(first));
    CHECK(t.size() == 2);

    // update переиспользует объекты с тем же host:port
    t.update(make({1, 3, 4}));
    CHECK(t.size() == 3);
    CHECK(t.snapshot()[0] == keep);
    CHECK(t.snapshot()[0]->active_connections == 7);

    auto picked = t.select(Balancer(Algorithm::LeastConnections));
    CHECK(picked.has_value() && (*picked)->port != 1);
}

void test_config() {
    auto ok = parse_backends("# comment\n127.0.0.1:9001\n  example.com:80  # tail\n\n");
    CHECK(ok.has_value() && ok->size() == 2);
    CHECK((*ok)[1]->host == "example.com" && (*ok)[1]->port == 80);

    CHECK(!parse_backends("").has_value());
    CHECK(!parse_backends("nohostport\n").has_value());
    CHECK(!parse_backends("host:0\n").has_value());
    CHECK(!parse_backends("host:70000\n").has_value());
    CHECK(!parse_backends("host:12ab\n").has_value());
    CHECK(!load_backends("/definitely/not/here.conf").has_value());
}

}  // namespace

int main() {
    test_parser_ok();
    test_parser_errors();
    test_parser_concept_overload();
    test_round_robin();
    test_least_connections();
    test_routing_table();
    test_config();
    if (g_failed == 0) std::cout << "all tests passed\n";
    return g_failed == 0 ? 0 : 1;
}
