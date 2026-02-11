#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>
#include <string>

//g++ -std=c++17 main.cpp -o server -lboost_system -lpthread
//./server

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp = asio::ip::tcp;

// ------------------------- HTTP Logic -------------------------

template <class Body, class Allocator>
http::response<http::string_body> handle_request(
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    // Базовый ответ (будем менять ниже)
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, "beast-minimal");
    res.keep_alive(req.keep_alive()); // поддержка keep-alive

    // Только GET
    if (req.method() != http::verb::get) {
        res.result(http::status::method_not_allowed);
        res.set(http::field::content_type, "text/plain; charset=utf-8");
        res.body() = "Only GET is supported";
        res.prepare_payload();
        return res;
    }

    // Роут: /status
    if (req.target() == "/status") {
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.body() = R"({"status":"ok"})";
        res.prepare_payload();
        return res;
    }

    // Всё остальное — 404
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain; charset=utf-8");
    res.body() = "Not found";
    res.prepare_payload();
    return res;
}

// ------------------------- Session -------------------------

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(tcp::socket socket)
        : socket_(std::move(socket)) {}

    void run() { do_read(); }

private:
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<void> res_holder_; // держим response живым до окончания async_write

    void do_read() {
        req_ = {}; // очистить прошлый запрос

        http::async_read(
            socket_, buffer_, req_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes) {
                self->on_read(ec, bytes);
            });
    }

    void on_read(beast::error_code ec, std::size_t /*bytes*/) {
        if (ec == http::error::end_of_stream) {
            return do_close();
        }
        if (ec) {
            std::cerr << "read error: " << ec.message() << "\n";
            return;
        }

        auto res = std::make_shared<http::response<http::string_body>>(
            handle_request(std::move(req_)));

        res_holder_ = res;

        http::async_write(
            socket_, *res,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes) {
                self->on_write(ec, bytes);
            });
    }

    void on_write(beast::error_code ec, std::size_t /*bytes*/) {
        if (ec) {
            std::cerr << "write error: " << ec.message() << "\n";
            return;
        }

        // Response уже отправлен — можно отпустить
        res_holder_.reset();

        // Если соединение не keep-alive — закрываем
        if (!req_.keep_alive()) {
            return do_close();
        }

        // Иначе ждём следующий запрос на том же сокете
        do_read();
    }

    void do_close() {
        beast::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_send, ec);
        // ignore ec
    }
};

// ------------------------- Listener -------------------------

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(asio::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc)
    {
        beast::error_code ec;

        acceptor_.open(endpoint.protocol(), ec);
        if (ec) throw beast::system_error(ec);

        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec) throw beast::system_error(ec);

        acceptor_.bind(endpoint, ec);
        if (ec) throw beast::system_error(ec);

        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) throw beast::system_error(ec);
    }

    void run() { do_accept(); }

private:
    tcp::acceptor acceptor_;

    void do_accept() {
        acceptor_.async_accept(
            [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
                self->on_accept(ec, std::move(socket));
            });
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            std::cerr << "accept error: " << ec.message() << "\n";
        } else {
            std::make_shared<HttpSession>(std::move(socket))->run();
        }
        do_accept(); // принимать следующих клиентов
    }
};

// ------------------------- main -------------------------

int main() {
    try {
        asio::io_context ioc;

        auto endpoint = tcp::endpoint{tcp::v4(), 8080};
        std::make_shared<Listener>(ioc, endpoint)->run();

        std::cout << "Server: http://localhost:8080\n";
        std::cout << "Try:    http://localhost:8080/status\n";

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
