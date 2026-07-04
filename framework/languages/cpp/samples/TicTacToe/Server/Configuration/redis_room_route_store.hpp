/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "sample_topology.hpp"

#include <boost/asio.hpp>

#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zlink::samples::tictactoe
{

struct room_route_t
{
    std::string room_id;
    std::string owner_play_endpoint;
    std::string owner_spot_node_rid;
};

class redis_room_route_store_t
{
  public:
    explicit redis_room_route_store_t (sample_topology_t topology) : _topology (std::move (topology)) {}

    void save (const room_route_t &route)
    {
        const auto stored = execute ({"SET", key (route.room_id), encode_route (route)});
        if (stored != "OK") {
            throw std::runtime_error ("Redis did not store TicTacToe room route.");
        }
    }

    room_route_t require (std::string_view room_id)
    {
        const auto stored = execute ({"GET", key (room_id)});
        if (stored.empty ()) {
            throw std::runtime_error ("TicTacToe room route was not found in Redis.");
        }
        return decode_route (stored);
    }

  private:
    std::string key (std::string_view room_id) const
    {
        return _topology.redis_key_prefix + std::string (room_id);
    }

    static std::pair<std::string, std::string> split_endpoint (std::string endpoint)
    {
        if (endpoint.rfind ("redis://", 0) == 0) {
            endpoint = endpoint.substr (8);
        }
        const auto separator = endpoint.rfind (':');
        if (separator == std::string::npos || separator == 0 || separator + 1 == endpoint.size ()) {
            throw std::runtime_error ("Redis endpoint must use host:port.");
        }
        return {endpoint.substr (0, separator), endpoint.substr (separator + 1)};
    }

    static std::string encode_route (const room_route_t &route)
    {
        return route.room_id + "\n" + route.owner_play_endpoint + "\n" + route.owner_spot_node_rid;
    }

    static room_route_t decode_route (const std::string &stored)
    {
        const auto first = stored.find ('\n');
        const auto second = first == std::string::npos ? std::string::npos
                                                       : stored.find ('\n', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            throw std::runtime_error ("Redis TicTacToe room route has an invalid format.");
        }
        return {stored.substr (0, first),
                stored.substr (first + 1, second - first - 1),
                stored.substr (second + 1)};
    }

    std::string execute (const std::vector<std::string> &args)
    {
        const auto [host, port] = split_endpoint (_topology.redis_endpoint);
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver (io);
        boost::asio::ip::tcp::socket socket (io);
        boost::asio::connect (socket, resolver.resolve (host, port));
        boost::asio::write (socket, boost::asio::buffer (encode_command (args)));
        boost::asio::streambuf buffer;
        return read_response (socket, buffer);
    }

    static std::string encode_command (const std::vector<std::string> &args)
    {
        std::string command = "*" + std::to_string (args.size ()) + "\r\n";
        for (const auto &arg : args) {
            command += "$" + std::to_string (arg.size ()) + "\r\n";
            command += arg;
            command += "\r\n";
        }
        return command;
    }

    static std::string read_line (boost::asio::ip::tcp::socket &socket,
                                  boost::asio::streambuf &buffer)
    {
        boost::asio::read_until (socket, buffer, "\r\n");
        std::istream input (&buffer);
        std::string line;
        std::getline (input, line);
        if (!line.empty () && line.back () == '\r') {
            line.pop_back ();
        }
        return line;
    }

    static std::string read_response (boost::asio::ip::tcp::socket &socket,
                                      boost::asio::streambuf &buffer)
    {
        const auto header = read_line (socket, buffer);
        if (header.empty ()) {
            throw std::runtime_error ("Redis returned an empty response.");
        }
        if (header[0] == '+') {
            return header.substr (1);
        }
        if (header[0] == '$') {
            const auto length = std::stoi (header.substr (1));
            if (length < 0) {
                return {};
            }
            while (buffer.size () < static_cast<std::size_t> (length + 2)) {
                boost::asio::read (socket, buffer,
                                   boost::asio::transfer_at_least (
                                     static_cast<std::size_t> (length + 2) - buffer.size ()));
            }
            std::string value (static_cast<std::size_t> (length), '\0');
            std::istream input (&buffer);
            input.read (value.data (), length);
            char cr = 0;
            char lf = 0;
            input.get (cr);
            input.get (lf);
            return value;
        }
        if (header[0] == '-') {
            throw std::runtime_error ("Redis error: " + header.substr (1));
        }
        throw std::runtime_error ("Unsupported Redis response: " + header);
    }

    sample_topology_t _topology;
};

} // namespace zlink::samples::tictactoe
