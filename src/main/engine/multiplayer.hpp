/***************************************************************************
    CannonBall DX experimental two-player multiplayer prototype.

    This first step deliberately keeps both game instances authoritative for
    their own physics. It exchanges only the state needed to draw the remote
    Ferrari in the local road scene.

    Configuration is intentionally kept in a tiny multiplayer.cfg next to the
    game for this test branch. This lets two copied CannonBall folders use
    different master/slave settings without touching the normal config.xml.
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
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"

namespace multiplayer_detail
{
    constexpr uint32_t MAGIC = 0x43424458; // "CBDX"
    constexpr uint16_t VERSION = 1;
    constexpr size_t PACKET_SIZE = 32;
    constexpr uint16_t DEFAULT_PORT = 51337;
    constexpr int PEER_TIMEOUT_MS = 1500;

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
    };

    struct State
    {
        uint32_t sequence = 0;
        uint8_t role = ROLE_OFF;
        bool active = false;
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
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(),
            [&](unsigned char c) { return !is_space(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(),
            [&](unsigned char c) { return !is_space(c); }).base(), value.end());
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

            const size_t equals = line.find('=');
            if (equals == std::string::npos)
                continue;

            const std::string key = lower(trim(line.substr(0, equals)));
            const std::string value = trim(line.substr(equals + 1));
            const std::string value_lower = lower(value);

            if (key == "enabled")
            {
                settings.enabled =
                    value_lower == "1" || value_lower == "true" ||
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
            else if (key == "host")
            {
                if (!value.empty())
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
                catch (...)
                {
                    // Keep the default for malformed prototype config values.
                }
            }
        }

        if (settings.role == ROLE_OFF)
            settings.enabled = false;

        return settings;
    }

    inline void write_u16(uint8_t* dst, uint16_t value)
    {
        dst[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
        dst[1] = static_cast<uint8_t>(value & 0xFF);
    }

    inline void write_u32(uint8_t* dst, uint32_t value)
    {
        dst[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
        dst[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
        dst[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
        dst[3] = static_cast<uint8_t>(value & 0xFF);
    }

    inline uint16_t read_u16(const uint8_t* src)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(src[0]) << 8) |
                                     static_cast<uint16_t>(src[1]));
    }

    inline uint32_t read_u32(const uint8_t* src)
    {
        return (static_cast<uint32_t>(src[0]) << 24) |
               (static_cast<uint32_t>(src[1]) << 16) |
               (static_cast<uint32_t>(src[2]) << 8) |
               static_cast<uint32_t>(src[3]);
    }

    inline std::array<uint8_t, PACKET_SIZE> encode(const State& state)
    {
        std::array<uint8_t, PACKET_SIZE> data{};
        size_t p = 0;

        write_u32(&data[p], MAGIC); p += 4;
        write_u16(&data[p], VERSION); p += 2;
        data[p++] = state.role;
        data[p++] = state.active ? 1 : 0;
        write_u32(&data[p], state.sequence); p += 4;
        data[p++] = state.game_state;
        data[p++] = state.mode;
        write_u16(&data[p], static_cast<uint16_t>(state.stage_lookup_off)); p += 2;
        write_u16(&data[p], static_cast<uint16_t>(state.stage)); p += 2;
        write_u32(&data[p], static_cast<uint32_t>(state.road_pos)); p += 4;
        write_u16(&data[p], static_cast<uint16_t>(state.car_x)); p += 2;
        write_u16(&data[p], state.speed); p += 2;
        write_u16(&data[p], static_cast<uint16_t>(state.steering)); p += 2;
        data[p++] = static_cast<uint8_t>(state.route_selected);
        data[p++] = state.split_state;
        write_u16(&data[p], state.ferrari_pal);

        return data;
    }

    inline bool decode(const uint8_t* data, size_t size, State& state)
    {
        if (!data || size != PACKET_SIZE)
            return false;

        size_t p = 0;
        if (read_u32(&data[p]) != MAGIC)
            return false;
        p += 4;

        if (read_u16(&data[p]) != VERSION)
            return false;
        p += 2;

        state.role = data[p++];
        state.active = (data[p++] & 1) != 0;
        state.sequence = read_u32(&data[p]); p += 4;
        state.game_state = data[p++];
        state.mode = data[p++];
        state.stage_lookup_off = static_cast<int16_t>(read_u16(&data[p])); p += 2;
        state.stage = static_cast<int16_t>(read_u16(&data[p])); p += 2;
        state.road_pos = static_cast<int32_t>(read_u32(&data[p])); p += 4;
        state.car_x = static_cast<int16_t>(read_u16(&data[p])); p += 2;
        state.speed = read_u16(&data[p]); p += 2;
        state.steering = static_cast<int16_t>(read_u16(&data[p])); p += 2;
        state.route_selected = static_cast<int8_t>(data[p++]);
        state.split_state = data[p++];
        state.ferrari_pal = read_u16(&data[p]);

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
        u_long mode = 1;
        return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    }

    inline bool resolve_ipv4(const std::string& host, uint16_t port, sockaddr_in& out)
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        addrinfo* result = nullptr;
        const std::string service = std::to_string(port);
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 || !result)
            return false;

        std::memcpy(&out, result->ai_addr, sizeof(sockaddr_in));
        freeaddrinfo(result);
        return true;
    }

    inline int lane_offset(Role role)
    {
        // OutRun's positive car-X direction is visually left. Give the two
        // roles a small conceptual lane offset so two freshly started games
        // are immediately visible next to one another instead of overlapping.
        if (role == ROLE_MASTER)
            return 0x30;
        if (role == ROLE_SLAVE)
            return -0x30;
        return 0;
    }
}

class Multiplayer
{
public:
    Multiplayer() = default;

    ~Multiplayer()
    {
        shutdown();
    }

    void tick()
    {
        if (!settings_loaded)
        {
            settings = multiplayer_detail::load_settings();
            settings_loaded = true;

            if (settings.enabled)
            {
                std::cout << "[Multiplayer] Prototype enabled as "
                          << (settings.role == multiplayer_detail::ROLE_MASTER ? "MASTER" : "SLAVE")
                          << " on UDP port " << settings.port << std::endl;
            }
        }

        if (!settings.enabled)
            return;

        if (!ensure_socket())
            return;

        receive_packets();
        send_state();
        update_connection_state();
        draw_remote_ferrari();
    }

    bool connected() const
    {
        return peer_connected;
    }

private:
    using clock = std::chrono::steady_clock;

    multiplayer_detail::Settings settings;
    multiplayer_detail::State remote;
    multiplayer_detail::socket_t socket = multiplayer_detail::INVALID_SOCKET_VALUE;
    sockaddr_in peer_addr{};
    bool peer_addr_valid = false;
    bool settings_loaded = false;
    bool socket_ready = false;
    bool peer_connected = false;
    bool have_remote = false;
    bool socket_error_reported = false;
    uint32_t sequence = 0;
    clock::time_point last_received{};
#ifdef _WIN32
    bool winsock_started = false;
#endif

    void shutdown()
    {
        multiplayer_detail::close_socket(socket);
        socket_ready = false;
#ifdef _WIN32
        if (winsock_started)
        {
            WSACleanup();
            winsock_started = false;
        }
#endif
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
                report_socket_error("WSAStartup failed");
                return false;
            }
            winsock_started = true;
        }
#endif

        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == multiplayer_detail::INVALID_SOCKET_VALUE)
        {
            report_socket_error("could not create UDP socket");
            return false;
        }

        if (!multiplayer_detail::set_nonblocking(socket))
        {
            report_socket_error("could not make UDP socket non-blocking");
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
                report_socket_error("could not bind UDP port (is another master using it?)");
                multiplayer_detail::close_socket(socket);
                return false;
            }

            std::cout << "[Multiplayer] Waiting for slave on UDP "
                      << settings.port << std::endl;
        }
        else
        {
            if (!multiplayer_detail::resolve_ipv4(settings.host, settings.port, peer_addr))
            {
                report_socket_error("could not resolve master host " + settings.host);
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

    void report_socket_error(const std::string& message)
    {
        if (!socket_error_reported)
        {
            std::cerr << "[Multiplayer] " << message << std::endl;
            socket_error_reported = true;
        }
    }

    multiplayer_detail::State local_state()
    {
        multiplayer_detail::State state;
        state.sequence = ++sequence;
        state.role = settings.role;
        state.active = outrun.game_state >= GS_START1 && outrun.game_state <= GS_INGAME;
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
        return state;
    }

    void send_state()
    {
        if (!peer_addr_valid)
            return;

        const auto packet = multiplayer_detail::encode(local_state());
        ::sendto(
            socket,
            reinterpret_cast<const char*>(packet.data()),
            static_cast<int>(packet.size()),
            0,
            reinterpret_cast<const sockaddr*>(&peer_addr),
            sizeof(peer_addr));
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
            if (!multiplayer_detail::decode(packet.data(), static_cast<size_t>(received), incoming))
                continue;

            if (incoming.role == settings.role)
                continue;

            // The master learns the slave's actual source port. This makes two
            // local instances work without assigning two different listen ports.
            if (settings.role == multiplayer_detail::ROLE_MASTER)
            {
                peer_addr = from;
                peer_addr_valid = true;
            }

            remote = incoming;
            have_remote = true;
            last_received = clock::now();
        }
    }

    void update_connection_state()
    {
        bool now_connected = false;
        if (have_remote)
        {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                clock::now() - last_received).count();
            now_connected = age <= multiplayer_detail::PEER_TIMEOUT_MS;
        }

        if (now_connected == peer_connected)
            return;

        peer_connected = now_connected;
        if (peer_connected)
            std::cout << "[Multiplayer] Peer connected" << std::endl;
        else
            std::cout << "[Multiplayer] Peer connection lost" << std::endl;
    }

    int road_curve_only(uint16_t z) const
    {
        // road0_h contains curve + local car/camera displacement - road width.
        // Remove that displacement to recover the road curve at this depth.
        const int32_t local_term =
            static_cast<int32_t>(oroad.car_x_bak) -
            static_cast<int32_t>(oroad.road_width_bak) +
            static_cast<int32_t>(oinitengine.camera_x_off);

        return static_cast<int32_t>(oroad.road0_h[z]) -
               ((local_term * static_cast<int32_t>(z)) >> 9);
    }

    void draw_remote_ferrari()
    {
        if (!peer_connected || !remote.active)
            return;

        if (outrun.game_state < GS_START1 || outrun.game_state > GS_INGAME)
            return;

        if (remote.mode != static_cast<uint8_t>(outrun.cannonball_mode))
            return;

        // For the first prototype both instances still progress independently.
        // Do not draw a car against a different stage's road data.
        if (remote.stage_lookup_off != oroad.stage_lookup_off)
            return;

        const int64_t road_delta =
            static_cast<int64_t>(remote.road_pos) -
            static_cast<int64_t>(static_cast<int32_t>(oroad.road_pos));

        // Approximate world distance -> OutRun sprite depth. Equal road
        // positions sit near the camera; a car roughly 60 road units ahead is
        // near the horizon. A car behind the camera is intentionally hidden.
        const int32_t depth_delta = static_cast<int32_t>((road_delta * 8) >> 16);
        const int32_t z_calc = 0x1E0 - depth_delta;
        if (z_calc < 4 || z_calc >= 0x1FC)
            return;

        const uint16_t z = static_cast<uint16_t>(z_calc);

        const int local_world_x =
            static_cast<int>(oinitengine.car_x_pos) +
            multiplayer_detail::lane_offset(settings.role);
        const int remote_world_x =
            static_cast<int>(remote.car_x) +
            multiplayer_detail::lane_offset(
                static_cast<multiplayer_detail::Role>(remote.role));

        int32_t screen_x = road_curve_only(z);
        screen_x += ((local_world_x - remote_world_x) * static_cast<int32_t>(z)) >> 9;

        // Reuse an otherwise unused special sprite slot. JUMP_ENTRIES_TOTAL
        // deliberately has two entries after SPRITE_FLAG available here.
        constexpr uint8_t REMOTE_SPRITE = OSprites::SPRITE_FLAG + 1;
        oentry* sprite = &osprites.jump_table[REMOTE_SPRITE];
        sprite->init(REMOTE_SPRITE);
        sprite->control = OSprites::ENABLE | OSprites::SHADOW;
        sprite->draw_props = oentry::BOTTOM;
        sprite->shadow = 3;
        sprite->priority = z;
        sprite->road_priority = z;
        sprite->y = -(oroad.road_y[oroad.road_p0 + z] >> 4) + 223;

        uint16_t zoom = static_cast<uint16_t>((z >> 2) + 4);
        if (zoom > 0x7F)
            zoom = 0x7F;
        sprite->zoom = static_cast<uint8_t>(zoom);

        int16_t turn = remote.steering >> 2;
        int16_t abs_turn = turn < 0 ? static_cast<int16_t>(-turn) : turn;
        int16_t turn_frame_offset = 0;
        if (abs_turn >= 0x12)
            turn_frame_offset += 0x18;
        if (abs_turn >= 0x1E)
            turn_frame_offset += 0x18;

        const uint32_t frame = outrun.adr.sprite_ferrari_frames +
                               static_cast<uint32_t>(turn_frame_offset) + 8;
        sprite->addr = roms.rom0p->read32(frame);

        int16_t frame_x = static_cast<int16_t>(roms.rom0p->read16(frame + 6));
        if (turn < 0)
        {
            sprite->control |= OSprites::HFLIP;
            frame_x = static_cast<int16_t>(-frame_x);
        }
        else
        {
            sprite->control &= ~OSprites::HFLIP;
        }

        // Ferrari frame offsets are authored for full-size zoom. Scale the
        // small steering offset along with the remote car's perspective size.
        screen_x += (static_cast<int32_t>(frame_x) * zoom) / 0x7F;
        sprite->x = static_cast<int16_t>(screen_x);

        const uint16_t wheel_frame = remote.speed > 0 ? (remote.sequence & 1) : 0;
        sprite->pal_src = static_cast<uint16_t>(remote.ferrari_pal + wheel_frame);
        osprites.map_palette(sprite);
        osprites.do_spr_order_shadows(sprite);
    }
};

inline Multiplayer multiplayer;
