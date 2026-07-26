/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Application/RoomAllocation/bingo_match_queue.hpp"
#include "../../../Configuration/sample_topology.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <istream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::bingo
{

class redis_bingo_match_queue_t final : public bingo_match_queue_t
{
  public:
    explicit redis_bingo_match_queue_t (const sample_topology_t &topology) : _topology (topology) {}

    bingo_match_reservation_t reserve (const std::string &mode,
                                       const std::string &actor_id,
                                       const std::string &new_room_id,
                                       int required_players) override
    {
        const auto now = std::to_string (std::chrono::duration_cast<std::chrono::milliseconds> (
                                           std::chrono::system_clock::now ().time_since_epoch ())
                                           .count ());
        auto reply =
          execute ({"EVAL", script (), "1", match_key (mode), actor_id, new_room_id,
                    std::to_string (required_players), now});
        if (reply.size () != 1) {
            throw std::runtime_error ("Redis match queue returned an invalid reservation.");
        }
        return {reply[0]};
    }

  private:
    std::string match_key (const std::string &mode) const
    {
        return _topology.redis_key_prefix + "match:" + mode;
    }

    static std::string script ()
    {
        return R"(local key = KEYS[1]
local actorId = ARGV[1]
local newRoomId = ARGV[2]
local requiredPlayers = tonumber(ARGV[3])
local nowMs = ARGV[4]

local roomId = redis.call('HGET', key, 'RoomId')
if not roomId then
  redis.call('HMSET', key,
    'RoomId', newRoomId,
    'ReservedActorIds', actorId,
    'RequiredPlayers', requiredPlayers,
    'CreatedAtUnixMs', nowMs)
  return { newRoomId }
end

local actors = redis.call('HGET', key, 'ReservedActorIds') or ''
local needle = '|' .. actorId .. '|'
if string.find('|' .. actors .. '|', needle, 1, true) then
  return { roomId }
end

local count = 0
for _ in string.gmatch(actors, '[^|]+') do
  count = count + 1
end

if count >= requiredPlayers then
  redis.call('HMSET', key,
    'RoomId', newRoomId,
    'ReservedActorIds', actorId,
    'RequiredPlayers', requiredPlayers,
    'CreatedAtUnixMs', nowMs)
  return { newRoomId }
end

if actors == '' then
  actors = actorId
else
  actors = actors .. '|' .. actorId
end
redis.call('HSET', key, 'ReservedActorIds', actors)
if count + 1 >= requiredPlayers then
  redis.call('DEL', key)
end
return { roomId })";
    }

    static std::pair<std::string, std::string> split_endpoint (std::string endpoint)
    {
        if (endpoint.rfind ("tcp://", 0) == 0) {
            endpoint = endpoint.substr (6);
        } else if (endpoint.rfind ("redis://", 0) == 0) {
            endpoint = endpoint.substr (8);
        }
        const auto separator = endpoint.rfind (':');
        if (separator == std::string::npos || separator == 0 || separator + 1 == endpoint.size ()) {
            throw std::runtime_error ("Redis endpoint must use host:port");
        }
        return {endpoint.substr (0, separator), endpoint.substr (separator + 1)};
    }

    std::vector<std::string> execute (const std::vector<std::string> &args) const
    {
        const auto [host, port] = split_endpoint (_topology.redis_endpoint);
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver (io);
        boost::asio::ip::tcp::socket socket (io);
        boost::asio::connect (socket, resolver.resolve (host, port));
        boost::asio::write (socket, boost::asio::buffer (encode_command (args)));
        boost::asio::streambuf buffer;
        return read_array (socket, buffer);
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

    static std::string read_bulk (boost::asio::ip::tcp::socket &socket,
                                  boost::asio::streambuf &buffer,
                                  const std::string &header)
    {
        const auto length = std::stoll (header.substr (1));
        if (length < 0) {
            return {};
        }
        while (buffer.size () < static_cast<std::size_t> (length + 2)) {
            boost::asio::read (socket, buffer, boost::asio::transfer_at_least (1));
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

    static std::vector<std::string> read_array (boost::asio::ip::tcp::socket &socket,
                                                boost::asio::streambuf &buffer)
    {
        const auto header = read_line (socket, buffer);
        if (header.empty ()) {
            throw std::runtime_error ("Redis returned an empty response");
        }
        if (header[0] == '-') {
            throw std::runtime_error ("Redis command failed: " + header.substr (1));
        }
        if (header[0] != '*') {
            throw std::runtime_error ("Redis response was not an array");
        }
        const auto count = std::stoll (header.substr (1));
        std::vector<std::string> values;
        values.reserve (static_cast<std::size_t> (count));
        for (long long index = 0; index < count; ++index) {
            const auto item = read_line (socket, buffer);
            if (item.empty () || item[0] != '$') {
                throw std::runtime_error ("Redis response item was not a bulk string");
            }
            values.push_back (read_bulk (socket, buffer, item));
        }
        return values;
    }

    sample_topology_t _topology;
};

} // namespace zlink::samples::bingo
