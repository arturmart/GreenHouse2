#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include "GlobalState.hpp"

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp = asio::ip::tcp;

// -------------------------- JSON HELPERS --------------------------

static inline std::string jescape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

static inline std::string any_to_json(const std::any& a) {
    if (!a.has_value())
        return "{\"type\":\"null\",\"value\":null}";

    const auto& t = a.type();

    if (t == typeid(bool)) {
        bool v = std::any_cast<bool>(a);
        return std::string("{\"type\":\"bool\",\"value\":") + (v ? "true}" : "false}");
    }
    if (t == typeid(int)) {
        int v = std::any_cast<int>(a);
        return "{\"type\":\"int\",\"value\":" + std::to_string(v) + "}";
    }
    if (t == typeid(double)) {
        double v = std::any_cast<double>(a);
        // std::to_string даёт много хвостов — но ок для простого API
        return "{\"type\":\"double\",\"value\":" + std::to_string(v) + "}";
    }
    if (t == typeid(std::string)) {
        std::string v = jescape(std::any_cast<std::string>(a));
        return "{\"type\":\"string\",\"value\":\"" + v + "\"}";
    }
    return "{\"type\":\"unknown\",\"value\":null}";
}

static inline std::string valueTypeToStr(GH_GlobalState::ValueType vt) {
    using VT = GH_GlobalState::ValueType;
    switch (vt) {
        case VT::BOOL:   return "bool";
        case VT::INT:    return "int";
        case VT::DOUBLE: return "double";
        case VT::STRING: return "string";
    }
    return "unknown";
}

static inline http::response<http::string_body>
make_json(http::request<http::string_body> const& req, http::status st, const std::string& body) {
    http::response<http::string_body> res{st, req.version()};
    res.set(http::field::content_type, "application/json; charset=utf-8");
    res.set(http::field::server, "gh-http");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}

static inline http::response<http::string_body>
make_text(http::request<http::string_body> const& req, http::status st, const std::string& body) {
    http::response<http::string_body> res{st, req.version()};
    res.set(http::field::content_type, "text/plain; charset=utf-8");
    res.set(http::field::server, "gh-http");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}

// -------------------------- SIMPLE JSON MAP PARSER --------------------------
// для POST можно допилить потом. Сейчас делаем GET-only как ты просил.
static inline std::unordered_map<std::string, std::string>
parse_json_map(const std::string& s) {
    std::unordered_map<std::string, std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        auto k1 = s.find('"', i);
        if (k1 == std::string::npos) break;
        auto k2 = s.find('"', k1 + 1);
        if (k2 == std::string::npos) break;
        std::string key = s.substr(k1 + 1, k2 - k1 - 1);

        auto c = s.find(':', k2);
        if (c == std::string::npos) break;

        size_t v1 = c + 1;
        while (v1 < s.size() && s[v1] == ' ') v1++;

        std::string value;

        if (v1 < s.size() && s[v1] == '"') {
            size_t v2 = s.find('"', v1 + 1);
            if (v2 == std::string::npos) break;
            value = s.substr(v1 + 1, v2 - v1 - 1);
            i = v2 + 1;
        } else {
            size_t v2 = s.find_first_of(",}", v1);
            if (v2 == std::string::npos) break;
            value = s.substr(v1, v2 - v1);
            i = v2;
        }

        // trim
        while (!value.empty() && value.back() == ' ') value.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());

        out[key] = value;
    }
    return out;
}

static inline std::any convert_any(const std::string& type, const std::string& value) {
    if (type == "bool") {
        if (value == "true" || value == "1") return std::any(true);
        if (value == "false" || value == "0") return std::any(false);
        throw std::runtime_error("Invalid bool: " + value);
    }
    if (type == "int")    return std::any(std::stoi(value));
    if (type == "double") return std::any(std::stod(value));
    if (type == "string") return std::any(value);
    throw std::runtime_error("Unsupported type: " + type);
}

// -------------------------- ROUTER --------------------------

static inline http::response<http::string_body>
handle_request(http::request<http::string_body>&& req) {
    const std::string target = std::string(req.target());
    const auto method = req.method();
    auto& st = GH_GlobalState::instance();

    // health
    if (method == http::verb::get && target == "/status")
        return make_json(req, http::status::ok, "{\"status\":\"ok\"}");

    // GET /schema/getters  -> keys from file
    if (method == http::verb::get && target == "/schema/getters") {
        auto schema = st.snapshotGetterSchema();
        std::string out = "{";
        bool first = true;
        for (const auto& kv : schema) {
            if (!first) out += ",";
            first = false;
            out += "\"" + jescape(kv.first) + "\":\"" + valueTypeToStr(kv.second) + "\"";
        }
        out += "}";
        return make_json(req, http::status::ok, out);
    }

    // GET /schema/executors -> names from file
    if (method == http::verb::get && target == "/schema/executors") {
        auto schema = st.snapshotExecSchemaByName();
        std::string out = "{";
        bool first = true;
        for (const auto& kv : schema) {
            if (!first) out += ",";
            first = false;
            out += "\"" + jescape(kv.first) + "\":\"" + valueTypeToStr(kv.second) + "\"";
        }
        out += "}";
        return make_json(req, http::status::ok, out);
    }

    // GET /getters -> all getters loaded from file (plus those created at runtime)
    if (method == http::verb::get && target == "/getters") {
        auto mp = st.snapshotGetters();
        std::string out = "{";
        bool first = true;
        for (const auto& kv : mp) {
            if (!first) out += ",";
            first = false;

            // kv.second is GetterEntry
            out += "\"" + jescape(kv.first) + "\":{";
            out += "\"valid\":" + std::string(kv.second.valid ? "true" : "false");
            out += ",\"stampMs\":" + std::to_string(kv.second.stampMs);
            out += ",\"data\":" + any_to_json(kv.second.value);
            out += "}";
        }
        out += "}";
        return make_json(req, http::status::ok, out);
    }

    // GET /getters/<key>
    if (method == http::verb::get && target.rfind("/getters/", 0) == 0) {
        const std::string key = target.substr(std::string("/getters/").size());
        try {
            auto e = st.getGetterEntry(key);
            std::string out = "{";
            out += "\"key\":\"" + jescape(key) + "\"";
            out += ",\"valid\":" + std::string(e.valid ? "true" : "false");
            out += ",\"stampMs\":" + std::to_string(e.stampMs);
            out += ",\"data\":" + any_to_json(e.value);
            out += "}";
            return make_json(req, http::status::ok, out);
        } catch (const std::exception& ex) {
            return make_json(req, http::status::not_found,
                std::string("{\"error\":\"") + jescape(ex.what()) + "\"}");
        }
    }

    // GET /executors
    if (method == http::verb::get && target == "/executors") {
        auto v = st.snapshotExecutors();
        std::string out = "[";
        bool first = true;
        for (const auto& e : v) {
            if (!first) out += ",";
            first = false;

            out += "{";
            out += "\"id\":" + std::to_string(e.id);
            out += ",\"name\":\"" + jescape(e.name) + "\"";
            out += ",\"valid\":" + std::string(e.entry.valid ? "true" : "false");
            out += ",\"stampMs\":" + std::to_string(e.entry.stampMs);
            out += ",\"mode\":\"" + jescape(toString(e.entry.mode)) + "\"";
            out += ",\"data\":" + any_to_json(e.entry.value);
            out += "}";
        }
        out += "]";
        return make_json(req, http::status::ok, out);
    }

    // POST endpoints можно включить позже (когда решим политику безопасности записи)
    return make_text(req, http::status::not_found, "Not found");
}

// -------------------------- SESSION + LISTENER --------------------------

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(tcp::socket socket) : socket_(std::move(socket)) {}
    void run() { do_read(); }

private:
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<void> hold_;

    void do_read() {
        req_ = {};
        http::async_read(socket_, buffer_, req_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                self->on_read(ec);
            });
    }

    void on_read(beast::error_code ec) {
        if (ec == http::error::end_of_stream) return do_close();
        if (ec) return;

        auto res = std::make_shared<http::response<http::string_body>>(
            handle_request(std::move(req_))
        );
        hold_ = res;

        http::async_write(socket_, *res,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                self->on_write(ec);
            });
    }

    void on_write(beast::error_code ec) {
        hold_.reset();
        if (ec) return;
        if (!req_.keep_alive()) return do_close();
        do_read();
    }

    void do_close() {
        beast::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_send, ec);
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(asio::io_context& ioc, tcp::endpoint ep)
        : ioc_(ioc), acceptor_(ioc) {
        beast::error_code ec;
        acceptor_.open(ep.protocol(), ec);
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        acceptor_.bind(ep, ec);
        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    }

    void run() { do_accept(); }

private:
    asio::io_context& ioc_;
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
            asio::make_strand(ioc_),
            [self = shared_from_this()](beast::error_code ec, tcp::socket s) {
                if (!ec) std::make_shared<HttpSession>(std::move(s))->run();
                self->do_accept();
            });
    }
};

class GH_HttpServer {
public:
    explicit GH_HttpServer(uint16_t port)
        : ioc_(1), port_(port) {}

    void start() {
        auto ep = tcp::endpoint(tcp::v4(), port_);
        listener_ = std::make_shared<Listener>(ioc_, ep);
        listener_->run();
    }

    // blocking
    void run() { ioc_.run(); }

    void stop() { ioc_.stop(); }

private:
    asio::io_context ioc_;
    uint16_t port_{8080};
    std::shared_ptr<Listener> listener_;
};
