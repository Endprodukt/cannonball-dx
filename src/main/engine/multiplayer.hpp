/***************************************************************************
    CannonBall DX experimental two-player multiplayer prototype.

    Each instance remains authoritative for its own physics. The prototype
    exchanges live car/road state over UDP, synchronizes the transition into
    a race, and renders the peer Ferrari as a real OutRun sprite.
***************************************************************************/

#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"

namespace multiplayer_detail
{
    constexpr uint32_t MAGIC = 0x43424458; // CBDX
    constexpr uint16_t VERSION = 2;
    constexpr std::size_t PACKET_SIZE = 40;
    constexpr uint16_t DEFAULT_PORT = 51337;
    constexpr int PEER_TIMEOUT_MS = 1500;
    constexpr int DEFAULT_CONNECT_TIMEOUT_MS = 15000;
    constexpr int DEFAULT_START_DELAY_MS = 1000;

    enum Role : uint8_t
    {
        ROLE_OFF = 0,
        ROLE_MASTER = 1,
        ROLE_SLAVE = 2,
    };

    struct Settings
    {
        bool enabled = false;
        Role role = ROLE_OFF;
        std::string host = "127.0.0.1";
        uint16_t port = DEFAULT_PORT;
        int connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS;
        int start_delay_ms = DEFAULT_START_DELAY_MS;
    };

    struct State
    {
        uint32_t sequence = 0;
        uint8_t role = ROLE_OFF;
        bool active = false;
        bool ready = false;
        uint8_t game_state = 0;
        uint8_t mode = 0;
        int16_t stage_lookup_off = 0;
        int16_t stage = 0;
        int32_t road_pos = 0;
        int16_t car_x = 0;
        uint16_t speed = 0;
        int16_t steering = 0;
        int8_t route_selected = 0;
        uint8_t split_state = 0;
        uint16_t ferrari_pal = OFerrari::PAL_RED;
        uint32_t start_token = 0;
        uint16_t start_after_ms = 0;
    };

#ifdef _WIN32
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif

    inline std::string trim(std::string value)
    {
        const auto non_space = [](unsigned char c) { return std::isspace(c) == 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), non_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(), value.end());
        return value;
    }

    inline std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    inline Settings load_settings()
    {
        Settings settings;
        std::ifstream file("multiplayer.cfg");
        if (!file)
            return settings;

        std::string line;
        while (std::getline(file, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            const std::size_t separator = line.find('=');
            if (separator == std::string::npos)
                continue;

            const std::string key = lower(trim(line.substr(0, separator)));
            const std::string value = trim(line.substr(separator + 1));
            const std::string value_lower = lower(value);

            if (key == "enabled")
            {
                settings.enabled = value_lower == "1" || value_lower == "true" ||
                                   value_lower == "yes" || value_lower == "on";
            }
            else if (key == "role")
            {
                if (value_lower == "master")
                    settings.role = ROLE_MASTER;
                else if (value_lower == "slave")
                    settings.role = ROLE_SLAVE;
                else
                    settings.role = ROLE_OFF;
            }
            else if (key == "host" && !value.empty())
            {
                settings.host = value;
            }
            else if (key == "port")
            {
                try
                {
                    const int port = std::stoi(value);
                    if (port >= 1 && port <= 65535)
                        settings.port = static_cast<uint16_t>(port);
                }
                catch (...) {}
            }
            else if (key == "timeout")
            {
                try
                {
                    const int seconds = std::stoi(value);
                    if (seconds >= 1 && seconds <= 120)
                        settings.connect_timeout_ms = seconds * 1000;
                }
                catch (...) {}
            }
            else if (key == "start_delay_ms")
            {
                try
                {
                    const int delay = std::stoi(value);
                    if (delay >= 100 && delay <= 5000)
                        settings.start_delay_ms = delay;
                }
                catch (...) {}
            }
        }

        if (settings.role == ROLE_OFF)
            settings.enabled = false;

        return settings;
    }

    inline void write16(uint8_t* dst, uint16_t value)
    {
        dst[0] = static_cast<uint8_t>(value >> 8);
        dst[1] = static_cast<uint8_t>(value);
    }

    inline void write32(uint8_t* dst, uint32_t value)
    {
        dst[0] = static_cast<uint8_t>(value >> 24);
        dst[1] = static_cast<uint8_t>(value >> 16);
        dst[2] = static_cast<uint8_t>(value >> 8);
        dst[3] = static_cast<uint8_t>(value);
    }

    inline uint16_t read16(const uint8_t* src)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(src[0]) << 8) | src[1]);
    }

    inline uint32_t read32(const uint8_t* src)
    {
        return (static_cast<uint32_t>(src[0]) << 24) |
               (static_cast<uint32_t>(src[1]) << 16) |
               (static_cast<uint32_t>(src[2]) << 8) |
               static_cast<uint32_t>(src[3]);
    }

    inline std::array<uint8_t, PACKET_SIZE> encode(const State& state)
    {
        std::array<uint8_t, PACKET_SIZE> data{};
        std::size_t p = 0;

        write32(&data[p], MAGIC); p += 4;
        write16(&data[p], VERSION); p += 2;
        data[p++] = state.role;
        data[p++] = static_cast<uint8_t>((state.active ? 1 : 0) | (state.ready ? 2 : 0));
        write32(&data[p], state.sequence); p += 4;
        data[p++] = state.game_state;
        data[p++] = state.mode;
        write16(&data[p], static_cast<uint16_t>(state.stage_lookup_off)); p += 2;
        write16(&data[p], static_cast<uint16_t>(state.stage)); p += 2;
        write32(&data[p], static_cast<uint32_t>(state.road_pos)); p += 4;
        write16(&data[p], static_cast<uint16_t>(state.car_x)); p += 2;
        write16(&data[p], state.speed); p += 2;
        write16(&data[p], static_cast<uint16_t>(state.steering)); p += 2;
        data[p++] = static_cast<uint8_t>(state.route_selected);
        data[p++] = state.split_state;
        write16(&data[p], state.ferrari_pal); p += 2;
        write32(&data[p], state.start_token); p += 4;
        write16(&data[p], state.start_after_ms); p += 2;
        write16(&data[p], 0);
        return data;
    }

    inline bool decode(const uint8_t* data, std::size_t size, State& state)
    {
        if (!data || size != PACKET_SIZE)
            return false;

        std::size_t p = 0;
        if (read32(&data[p]) != MAGIC)
            return false;
        p += 4;
        if (read16(&data[p]) != VERSION)
            return false;
        p += 2;

        state.role = data[p++];
        const uint8_t flags = data[p++];
        state.active = (flags & 1) != 0;
        state.ready = (flags & 2) != 0;
        state.sequence = read32(&data[p]); p += 4;
        state.game_state = data[p++];
        state.mode = data[p++];
        state.stage_lookup_off = static_cast<int16_t>(read16(&data[p])); p += 2;
        state.stage = static_cast<int16_t>(read16(&data[p])); p += 2;
        state.road_pos = static_cast<int32_t>(read32(&data[p])); p += 4;
        state.car_x = static_cast<int16_t>(read16(&data[p])); p += 2;
        state.speed = read16(&data[p]); p += 2;
        state.steering = static_cast<int16_t>(read16(&data[p])); p += 2;
        state.route_selected = static_cast<int8_t>(data[p++]);
        state.split_state = data[p++];
        state.ferrari_pal = read16(&data[p]); p += 2;
        state.start_token = read32(&data[p]); p += 4;
        state.start_after_ms = read16(&data[p]);

        return state.role == ROLE_MASTER || state.role == ROLE_SLAVE;
    }

    inline void close_socket(socket_t& socket)
    {
        if (socket == INVALID_SOCKET_VALUE)
            return;
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
        socket = INVALID_SOCKET_VALUE;
    }

    inline bool set_nonblocking(socket_t socket)
    {
#ifdef _WIN32
        u_long enabled = 1;
        return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    }

    inline bool resolve_ipv4(const std::string& host, uint16_t port, sockaddr_in& address)
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        addrinfo* result = nullptr;
        const std::string service = std::to_string(port);
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 || !result)
            return false;

        std::memcpy(&address, result->ai_addr, sizeof(sockaddr_in));
        freeaddrinfo(result);
        return true;
    }

    inline int lane_offset(Role role)
    {
        // Keep the two logical player origins separated by roughly one lane.
        // Positive car-X is visually left in OutRun.
        if (role == ROLE_MASTER)
            return 0x50;
        if (role == ROLE_SLAVE)
            return -0x50;
        return 0;
    }
}

class Multiplayer
{
public:
    Multiplayer() = default;
    ~Multiplayer() { shutdown(); }

    bool enabled()
    {
        load_settings_once();
        return settings.enabled;
    }

    bool connected() const { return peer_connected; }

    // Called from the application tick, including menus and Music Select. This
    // keeps the UDP session alive before either game has entered the race.
    void network_tick()
    {
        load_settings_once();
        if (!settings.enabled || !ensure_socket())
            return;

        receive_packets();
        update_connection_state();
        send_state();

        // A timed-out race stays single-player for that run, but returning to
        // Music Select arms multiplayer again for the next attempt.
        if (session_bypassed && outrun.game_state == GS_MUSIC)
            session_bypassed = false;
    }

    // Called immediately before Outrun::tick while GS_INIT_GAME is pending.
    // Returning true freezes the engine on the pre-race frame. Both instances
    // therefore initialize the road, traffic, intro animation and countdown on
    // the same scheduled launch instead of trying to correct them afterwards.
    bool hold_game_start()
    {
        load_settings_once();
        if (!settings.enabled || session_bypassed || outrun.game_state != GS_INIT_GAME)
        {
            race_waiting = false;
            return false;
        }

        const auto now = clock::now();
        if (!race_waiting)
        {
            race_waiting = true;
            wait_started = now;
            std::cout << "[Multiplayer] Race ready. Waiting for player 2 (timeout "
                      << (settings.connect_timeout_ms / 1000) << "s)" << std::endl;
        }

        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - wait_started).count();
        if (waited >= settings.connect_timeout_ms)
        {
            session_bypassed = true;
            race_waiting = false;
            start_scheduled = false;
            std::cout << "[Multiplayer] Start timeout - continuing this race without sync"
                      << std::endl;
            return false;
        }

        if (!peer_connected || !remote.ready)
            return true;

        if (settings.role == multiplayer_detail::ROLE_MASTER)
        {
            if (advertised_start_token <= consumed_start_token)
            {
                advertised_start_token = ++master_token_counter;
                if (advertised_start_token == 0)
                    advertised_start_token = ++master_token_counter;

                scheduled_start_token = advertised_start_token;
                start_deadline = now + std::chrono::milliseconds(settings.start_delay_ms);
                start_scheduled = true;
                std::cout << "[Multiplayer] Both players ready. Synchronized start in "
                          << settings.start_delay_ms << " ms" << std::endl;
            }
        }
        else
        {
            if (!start_scheduled || scheduled_start_token <= consumed_start_token)
                return true;
        }

        if (!start_scheduled || now < start_deadline)
            return true;

        consumed_start_token = scheduled_start_token;
        race_waiting = false;

        // Both engines now begin from the same deterministic random stream.
        // This is important because Attract Mode can otherwise consume a
        // different number of random values before the players press START.
        outils::reset_random_seed();
        outrun.tick_counter = 0;

        std::cout << "[Multiplayer] Synchronized race start" << std::endl;
        return false;
    }

    // Called after the local Ferrari has submitted its normal in-game sprite.
    // Network traffic itself is handled separately by network_tick().
    void draw_remote_ferrari()
    {
        if (session_bypassed || !peer_connected || !remote.active ||
            outrun.game_state != GS_INGAME ||
            remote.game_state != GS_INGAME ||
            remote.mode != static_cast<uint8_t>(outrun.cannonball_mode) ||
            remote.stage_lookup_off != oroad.stage_lookup_off ||
            oroad.road_ctrl == ORoad::ROAD_OFF)
        {
            return;
        }

        const int64_t road_delta = static_cast<int64_t>(remote.road_pos) -
                                   static_cast<int64_t>(static_cast<int32_t>(oroad.road_pos));
        const int32_t depth_delta = static_cast<int32_t>((road_delta * 8) >> 16);
        const int32_t z_calc = 0x1F0 - depth_delta;

        if (z_calc < 4 || z_calc >= 0x1FC)
            return;

        const uint16_t z = static_cast<uint16_t>(z_calc);
        const int local_x = static_cast<int>(oinitengine.car_x_pos) +
                            multiplayer_detail::lane_offset(settings.role);
        const int remote_x = static_cast<int>(remote.car_x) +
                             multiplayer_detail::lane_offset(
                                 static_cast<multiplayer_detail::Role>(remote.role));

        int32_t screen_x = road_curve_at(z) +
                           (((local_x - remote_x) * static_cast<int32_t>(z)) >> 9);

        constexpr uint8_t REMOTE_SPRITE = OSprites::SPRITE_FLAG + 1;
        oentry* sprite = &osprites.jump_table[REMOTE_SPRITE];
        sprite->init(REMOTE_SPRITE);
        sprite->control = OSprites::ENABLE | OSprites::SHADOW;
        sprite->draw_props = oentry::BOTTOM;
        sprite->shadow = 3;
        sprite->priority = z;
        sprite->road_priority = z;
        sprite->y = static_cast<int16_t>(oroad.get_road_y(z) - 2);

        uint16_t zoom = static_cast<uint16_t>((z >> 2) + 4);
        if (zoom > 0x7F)
            zoom = 0x7F;
        sprite->zoom = static_cast<uint8_t>(zoom);

        int16_t steering = remote.steering;
        if ((steering >= -8 && steering <= 7) || remote.speed < 0x14)
            steering = 0;
        const int16_t turn = steering >> 2;
        const int16_t abs_turn = turn < 0 ? static_cast<int16_t>(-turn) : turn;

        int16_t turn_frame_offset = 0;
        if (abs_turn >= 0x12) turn_frame_offset += 0x18;
        if (abs_turn >= 0x1E) turn_frame_offset += 0x18;

        const uint16_t slope_far = z >= 8 ? static_cast<uint16_t>(z - 8) : z;
        const int16_t slope = oroad.road_y[oroad.road_p0 + slope_far] -
                              oroad.road_y[oroad.road_p0 + z];
        int16_t incline_frame_offset = 0;
        if (slope >= 0x12) incline_frame_offset += 8;
        if (slope >= 0x13) incline_frame_offset += 8;

        const uint32_t frame = outrun.adr.sprite_ferrari_frames +
                               static_cast<uint32_t>(turn_frame_offset + incline_frame_offset);
        sprite->addr = roms.rom0p->read32(frame);

        int16_t frame_x = static_cast<int16_t>(roms.rom0p->read16(frame + 6));
        if (turn < 0)
        {
            sprite->control |= OSprites::HFLIP;
            frame_x = static_cast<int16_t>(-frame_x);
        }

        screen_x += (static_cast<int32_t>(frame_x) * zoom) / 0x7F;
        sprite->x = static_cast<int16_t>(screen_x);

        const uint16_t wheel_frame = remote.speed > 0 ? (remote.sequence & 1) : 0;
        sprite->pal_src = static_cast<uint16_t>(remote.ferrari_pal + wheel_frame);
        osprites.map_palette(sprite);
        osprites.do_spr_order_shadows(sprite);
    }

private:
    using clock = std::chrono::steady_clock;

    multiplayer_detail::Settings settings;
    multiplayer_detail::State remote;
    multiplayer_detail::socket_t socket = multiplayer_detail::INVALID_SOCKET_VALUE;
    sockaddr_in peer_addr{};
    bool settings_loaded = false;
    bool socket_ready = false;
    bool peer_addr_valid = false;
    bool have_remote = false;
    bool peer_connected = false;
    bool error_reported = false;
    bool race_waiting = false;
    bool session_bypassed = false;
    bool start_scheduled = false;
    uint32_t sequence = 0;
    uint32_t master_token_counter = 0;
    uint32_t advertised_start_token = 0;
    uint32_t scheduled_start_token = 0;
    uint32_t consumed_start_token = 0;
    clock::time_point last_received{};
    clock::time_point wait_started{};
    clock::time_point start_deadline{};
#ifdef _WIN32
    bool winsock_started = false;
#endif

    void load_settings_once()
    {
        if (settings_loaded)
            return;

        settings = multiplayer_detail::load_settings();
        settings_loaded = true;
        if (settings.enabled)
        {
            std::cout << "[Multiplayer] Prototype enabled as "
                      << (settings.role == multiplayer_detail::ROLE_MASTER ? "MASTER" : "SLAVE")
                      << " on UDP port " << settings.port << std::endl;
        }
    }

    void shutdown()
    {
        multiplayer_detail::close_socket(socket);
#ifdef _WIN32
        if (winsock_started)
            WSACleanup();
        winsock_started = false;
#endif
        socket_ready = false;
    }

    void error_once(const std::string& text)
    {
        if (!error_reported)
        {
            std::cerr << "[Multiplayer] " << text << std::endl;
            error_reported = true;
        }
    }

    bool ensure_socket()
    {
        if (socket_ready)
            return true;

#ifdef _WIN32
        if (!winsock_started)
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                error_once("WSAStartup failed");
                return false;
            }
            winsock_started = true;
        }
#endif

        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == multiplayer_detail::INVALID_SOCKET_VALUE)
        {
            error_once("could not create UDP socket");
            return false;
        }

        if (!multiplayer_detail::set_nonblocking(socket))
        {
            error_once("could not make UDP socket non-blocking");
            multiplayer_detail::close_socket(socket);
            return false;
        }

        if (settings.role == multiplayer_detail::ROLE_MASTER)
        {
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port = htons(settings.port);
            if (::bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
            {
                error_once("could not bind UDP port (is another master using it?)");
                multiplayer_detail::close_socket(socket);
                return false;
            }
            std::cout << "[Multiplayer] Waiting for slave on UDP " << settings.port << std::endl;
        }
        else
        {
            if (!multiplayer_detail::resolve_ipv4(settings.host, settings.port, peer_addr))
            {
                error_once("could not resolve master host " + settings.host);
                multiplayer_detail::close_socket(socket);
                return false;
            }
            peer_addr_valid = true;
            std::cout << "[Multiplayer] Connecting to master "
                      << settings.host << ':' << settings.port << std::endl;
        }

        socket_ready = true;
        return true;
    }

    multiplayer_detail::State make_local_state()
    {
        multiplayer_detail::State state;
        state.sequence = ++sequence;
        state.role = settings.role;
        state.active = outrun.game_state == GS_INGAME;
        state.ready = outrun.game_state == GS_INIT_GAME && !session_bypassed;
        state.game_state = static_cast<uint8_t>(outrun.game_state);
        state.mode = static_cast<uint8_t>(outrun.cannonball_mode);
        state.stage_lookup_off = oroad.stage_lookup_off;
        state.stage = ostats.cur_stage;
        state.road_pos = static_cast<int32_t>(oroad.road_pos);
        state.car_x = oinitengine.car_x_pos;
        state.speed = static_cast<uint16_t>(oinitengine.car_increment >> 16);
        state.steering = oinputs.steering_adjust;
        state.route_selected = oinitengine.route_selected;
        state.split_state = static_cast<uint8_t>(oinitengine.rd_split_state & 0xFF);
        state.ferrari_pal = oferrari.ferrari_pal;

        if (settings.role == multiplayer_detail::ROLE_MASTER &&
            advertised_start_token > consumed_start_token)
        {
            state.start_token = advertised_start_token;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                start_deadline - clock::now()).count();
            state.start_after_ms = static_cast<uint16_t>(
                remaining <= 0 ? 0 : std::min<int64_t>(remaining, 65535));
        }

        return state;
    }

    void send_state()
    {
        if (!peer_addr_valid)
            return;

        const auto packet = multiplayer_detail::encode(make_local_state());
        ::sendto(socket,
                 reinterpret_cast<const char*>(packet.data()),
                 static_cast<int>(packet.size()),
                 0,
                 reinterpret_cast<const sockaddr*>(&peer_addr),
                 static_cast<int>(sizeof(peer_addr)));
    }

    void receive_packets()
    {
        std::array<uint8_t, multiplayer_detail::PACKET_SIZE> packet{};
        for (;;)
        {
            sockaddr_in from{};
#ifdef _WIN32
            int from_len = sizeof(from);
#else
            socklen_t from_len = sizeof(from);
#endif
            const int received = static_cast<int>(::recvfrom(
                socket,
                reinterpret_cast<char*>(packet.data()),
                static_cast<int>(packet.size()),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &from_len));

            if (received <= 0)
                break;

            multiplayer_detail::State incoming;
            if (!multiplayer_detail::decode(packet.data(), static_cast<std::size_t>(received), incoming) ||
                incoming.role == settings.role)
            {
                continue;
            }

            if (settings.role == multiplayer_detail::ROLE_MASTER)
            {
                peer_addr = from;
                peer_addr_valid = true;
            }

            remote = incoming;
            have_remote = true;
            last_received = clock::now();

            if (settings.role == multiplayer_detail::ROLE_SLAVE &&
                outrun.game_state == GS_INIT_GAME &&
                incoming.start_token > consumed_start_token &&
                incoming.start_token != scheduled_start_token)
            {
                scheduled_start_token = incoming.start_token;
                start_deadline = clock::now() +
                                 std::chrono::milliseconds(incoming.start_after_ms);
                start_scheduled = true;
                std::cout << "[Multiplayer] Master scheduled synchronized start in "
                          << incoming.start_after_ms << " ms" << std::endl;
            }
        }
    }

    void update_connection_state()
    {
        bool connected_now = false;
        if (have_remote)
        {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                clock::now() - last_received).count();
            connected_now = age <= multiplayer_detail::PEER_TIMEOUT_MS;
        }

        if (connected_now == peer_connected)
            return;

        peer_connected = connected_now;
        if (!peer_connected)
        {
            start_scheduled = false;
            scheduled_start_token = 0;
            if (settings.role == multiplayer_detail::ROLE_MASTER &&
                advertised_start_token > consumed_start_token)
            {
                advertised_start_token = consumed_start_token;
            }
        }

        std::cout << (peer_connected ? "[Multiplayer] Peer connected"
                                     : "[Multiplayer] Peer connection lost")
                  << std::endl;
    }

    int32_t road_curve_at(uint16_t z) const
    {
        const int32_t camera = oinitengine.camera_x_off;
        const int32_t car = oroad.car_x_bak;
        const int32_t width = oroad.road_width_bak;

        if (oroad.road_ctrl == ORoad::ROAD_R1)
        {
            const int32_t displacement = ((car + width + camera) * z) >> 9;
            return static_cast<int32_t>(oroad.road1_h[z]) - displacement;
        }

        if (oroad.road_ctrl == ORoad::ROAD_R1_SPLIT)
        {
            const int32_t displacement = ((car + width + camera) * z) >> 9;
            return displacement - static_cast<int32_t>(oroad.road1_h[z]);
        }

        const int32_t displacement = ((car - width + camera) * z) >> 9;
        return static_cast<int32_t>(oroad.road0_h[z]) - displacement;
    }
};

inline Multiplayer multiplayer;
