#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

#include "../GlobalState.hpp"    // <-- твой файл

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp = asio::ip::tcp;

// -------------------------- JSON HELPERS --------------------------

static std::mutex g_state_mtx;

// simple JSON escape
static std::string jescape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c; break;
        }
    }
    return out;
}

// -------------------------- ANY → JSON --------------------------

static std::string any_to_json(const std::any& a)
{
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
        return "{\"type\":\"double\",\"value\":" + std::to_string(v) + "}";
    }
    if (t == typeid(std::string)) {
        std::string v = jescape(std::any_cast<std::string>(a));
        return "{\"type\":\"string\",\"value\":\"" + v + "\"}";
    }

    return "{\"type\":\"unknown\",\"value\":null}";
}

// -------------------------- HTTP RESPONSE HELPERS --------------------------

static http::response<http::string_body>
make_json(http::request<http::string_body> const& req, http::status st, const std::string& body)
{
    http::response<http::string_body> res{st, req.version()};
    res.set(http::field::content_type, "application/json; charset=utf-8");
    res.set(http::field::server, "gh-minimal-json");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}

static http::response<http::string_body>
make_text(http::request<http::string_body> const& req, http::status st, const std::string& body)
{
    http::response<http::string_body> res{st, req.version()};
    res.set(http::field::content_type, "text/plain; charset=utf-8");
    res.set(http::field::server, "gh-minimal");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}

// -------------------------- PARSE JSON (SIMPLE) --------------------------
//
// Ожидаем JSON вида:
// {"type":"int","value":123,"mode":"auto"}
//
// Примитивный парсер (для маленького API этого достаточно)

static std::unordered_map<std::string, std::string>
parse_json_map(const std::string& s)
{
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

        std::string value;

        if (s[v1] == '"') {
            size_t v2 = s.find('"', v1 + 1);
            value = s.substr(v1 + 1, v2 - v1 - 1);
            i = v2 + 1;
        } else {
            size_t v2 = s.find_first_of(",}", v1);
            value = s.substr(v1, v2 - v1);
            i = v2;
        }

        // trim spaces
        while (!value.empty() && (value.back() == ' ')) value.pop_back();
        while (!value.empty() && (value.front() == ' ')) value.erase(value.begin());

        out[key] = value;
    }

    return out;
}

// -------------------------- type conversion --------------------------

static std::any convert_any(const std::string& type, const std::string& value)
{
    if (type == "bool") {
        if (value == "true" || value == "1") return std::any(true);
        if (value == "false" || value == "0") return std::any(false);
        throw std::runtime_error("Invalid bool: " + value);
    }
    if (type == "int") {
        return std::any(std::stoi(value));
    }
    if (type == "double") {
        return std::any(std::stod(value));
    }
    if (type == "string") {
        return std::any(value);
    }

    throw std::runtime_error("Unsupported type: " + type);
}

// -------------------------- HTTP ROUTER --------------------------

static http::response<http::string_body>
handle_request(http::request<http::string_body>&& req)
{
    std::string target = std::string(req.target());
    auto method = req.method();

    // /status
    if (method == http::verb::get && target == "/status") {
        return make_json(req, http::status::ok, "{\"status\":\"ok\"}");
    }

    // GET /getters
    if (method == http::verb::get && target == "/getters") {
        std::lock_guard<std::mutex> lk(g_state_mtx);
        auto& st = GH_GlobalState::instance();

        std::string out = "{";
        bool first = true;
        for (auto& kv : st.getters()) {
            if (!first) out += ",";
            first = false;
            out += "\"" + jescape(kv.first) + "\":" + any_to_json(kv.second);
        }
        out += "}";

        return make_json(req, http::status::ok, out);
    }

    // GET /executors
    if (method == http::verb::get && target == "/executors") {
        std::lock_guard<std::mutex> lk(g_state_mtx);
        auto& st = GH_GlobalState::instance();

        // build id->name map
        std::unordered_map<int, std::string> id2name;
        for (auto& kv : st.execNameToId()) id2name[kv.second] = kv.first;

        std::string out = "[";

        bool first = true;
        for (auto& kv : st.executors()) {
            if (!first) out += ",";
            first = false;

            int id = kv.first;
            auto& tup = kv.second;

            out += "{";
            out += "\"id\":" + std::to_string(id);

            if (id2name.count(id)) {
                out += ",\"name\":\"" + jescape(id2name[id]) + "\"";
            }

            out += ",\"mode\":\"" + toString(std::get<1>(tup)) + "\"";

            out += ",\"data\":" + any_to_json(std::get<0>(tup));

            out += "}";
        }

        out += "]";
        return make_json(req, http::status::ok, out);
    }

    // POST /executors/<id>
    if (method == http::verb::post && target.rfind("/executors/", 0) == 0) {
        std::string idStr = target.substr(12);
        int id = std::stoi(idStr);

        auto mp = parse_json_map(req.body());

        if (!mp.count("type") || !mp.count("value"))
            return make_text(req, http::status::bad_request, "JSON must contain type,value");

        std::string type = mp["type"];
        std::string val  = mp["value"];

        bool hasMode = mp.count("mode");
        GH_MODE mode = GH_MODE::MANUAL;

        std::lock_guard<std::mutex> lk(g_state_mtx);
        auto& st = GH_GlobalState::instance();

        std::any a = convert_any(type, val);

        if (hasMode) {
            std::string m = mp["mode"];
            if (m == "manual" || m == "0") mode = GH_MODE::MANUAL;
            else if (m == "auto" || m == "1") mode = GH_MODE::AUTO;
            else return make_text(req, http::status::bad_request, "Invalid mode");
        } else {
            mode = st.getExecMode(id);
        }

        st.setExec(id, a, mode);

        return make_json(req, http::status::ok, "{\"ok\":true}");
    }

    // POST /getters/<key>
    if (method == http::verb::post && target.rfind("/getters/", 0) == 0) {
        std::string key = target.substr(9);

        auto mp = parse_json_map(req.body());
        if (!mp.count("type") || !mp.count("value"))
            return make_text(req, http::status::bad_request, "JSON must contain type,value");

        std::string type = mp["type"];
        std::string val  = mp["value"];

        std::any a = convert_any(type, val);

        std::lock_guard<std::mutex> lk(g_state_mtx);
        GH_GlobalState::instance().setGetter(key, a);

        return make_json(req, http::status::ok, "{\"ok\":true}");
    }

    // fallback
    return make_text(req, http::status::not_found, "Not found");
}

// -------------------------- SESSION + LISTENER --------------------------

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(tcp::socket socket)
        : socket_(std::move(socket)) {}

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

        auto res = std::make_shared<http::response<http::string_body>>(handle_request(std::move(req_)));
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
        : acceptor_(ioc)
    {
        beast::error_code ec;
        acceptor_.open(ep.protocol(), ec);
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        acceptor_.bind(ep, ec);
        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    }

    void run() { do_accept(); }

private:
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
            [self = shared_from_this()](beast::error_code ec, tcp::socket s) {
                if (!ec) std::make_shared<HttpSession>(std::move(s))->run();
                self->do_accept();
            });
    }
};

// -------------------------- MAIN --------------------------

int main() {
    asio::io_context ioc;

    auto ep = tcp::endpoint(tcp::v4(), 8080);

    std::make_shared<Listener>(ioc, ep)->run();

    std::cout << "HTTP server on http://localhost:8080\n";
    std::cout << "GET /getters\n";
    std::cout << "GET /executors\n";

    ioc.run();
    return 0;
}
