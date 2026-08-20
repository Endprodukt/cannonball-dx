#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// MAME-compatible external output transport for CannonBall-SE.
//
// Two transports can run at the same time:
//  * MAME network output protocol (TCP, default port 8000)
//  * MAME Windows output messages (MAMEHooker compatible, Windows only)
//
// The machine/game identifier intentionally uses "cannonball" so external
// tools can use a dedicated cannonball.ini rather than reusing outrun.ini.
class ExternalOutputs
{
public:
    ExternalOutputs()
        : network_listen(invalid_socket()),
          network_running(false),
          network_failed(false),
          network_port(0)
#ifdef _WIN32
        , winsock_started(false),
          window_handle(nullptr),
          window_running(false),
          om_mame_start(0),
          om_mame_stop(0),
          om_mame_update_state(0),
          om_mame_register_client(0),
          om_mame_unregister_client(0),
          om_mame_get_id_string(0)
#endif
    {
        for (auto& item : items)
        {
            item.value = 0;
            item.old_value = -1;
        }
    }

    ~ExternalOutputs()
    {
        shutdown();
    }

    void update(bool enable_network,
                bool enable_windows,
                int port,
                bool application_running,
                int start_lamp,
                int brake_lamp,
                int view_lamp,
                int view1_lamp,
                int view2_lamp,
                int view3_lamp)
    {
        if (!application_running)
        {
            shutdown();
            return;
        }

        if (port < 1 || port > 65535)
            port = 8000;

        ensure_network(enable_network, port);
        ensure_windows(enable_windows);

        if (network_running)
            accept_network_clients();
#ifdef _WIN32
        if (window_running)
            pump_windows_messages();
#endif

        const int values[ITEM_COUNT] = {
            start_lamp,
            brake_lamp,
            view_lamp,
            view1_lamp,
            view2_lamp,
            view3_lamp
        };

        for (std::size_t i = 0; i < items.size(); i++)
        {
            items[i].value = values[i];
            if (items[i].value != items[i].old_value)
            {
                if (network_running)
                    broadcast_network(items[i].name, items[i].value);
#ifdef _WIN32
                if (window_running)
                    broadcast_windows(items[i].id, items[i].value);
#endif
                items[i].old_value = items[i].value;
            }
        }
    }

    void shutdown()
    {
        shutdown_network();
#ifdef _WIN32
        shutdown_windows();
#endif
    }

private:
    static constexpr const char* MACHINE_NAME = "cannonball";
    static constexpr std::size_t ITEM_COUNT = 6;

    struct OutputItem
    {
        const char* name;
        std::uint32_t id;
        int value;
        int old_value;
    };

    std::array<OutputItem, ITEM_COUNT> items {{
        { "Start_lamp", 12345, 0, -1 },
        { "Brake_lamp", 12346, 0, -1 },
        { "View_lamp",  12347, 0, -1 },
        { "View1_lamp", 12348, 0, -1 },
        { "View2_lamp", 12349, 0, -1 },
        { "View3_lamp", 12350, 0, -1 },
    }};

#ifdef _WIN32
    using socket_type = SOCKET;
    static socket_type invalid_socket() { return INVALID_SOCKET; }
#else
    using socket_type = int;
    static socket_type invalid_socket() { return -1; }
#endif

    socket_type network_listen;
    std::vector<socket_type> network_clients;
    bool network_running;
    bool network_failed;
    int network_port;

#ifdef _WIN32
    bool winsock_started;
#endif

    static bool socket_would_block()
    {
#ifdef _WIN32
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    static void close_socket(socket_type socket)
    {
#ifdef _WIN32
        if (socket != INVALID_SOCKET)
            closesocket(socket);
#else
        if (socket >= 0)
            close(socket);
#endif
    }

    static bool set_nonblocking(socket_type socket)
    {
#ifdef _WIN32
        u_long mode = 1;
        return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    }

    void ensure_network(bool enabled, int port)
    {
        if (!enabled)
        {
            shutdown_network();
            network_failed = false;
            return;
        }

        if (network_running && port == network_port)
            return;

        if (network_running && port != network_port)
            shutdown_network();

        if (network_failed && port == network_port)
            return;

        network_port = port;
        init_network();
    }

    void init_network()
    {
#ifdef _WIN32
        if (!winsock_started)
        {
            WSADATA data {};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                std::cerr << "External outputs: unable to initialise Winsock." << std::endl;
                network_failed = true;
                return;
            }
            winsock_started = true;
        }
#endif

        network_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (network_listen == invalid_socket())
        {
            std::cerr << "External outputs: unable to create network output socket." << std::endl;
            network_failed = true;
            return;
        }

        int reuse = 1;
#ifdef _WIN32
        setsockopt(network_listen, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
        setsockopt(network_listen, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<std::uint16_t>(network_port));

        if (bind(network_listen, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(network_listen, 4) != 0 ||
            !set_nonblocking(network_listen))
        {
            std::cerr << "External outputs: unable to open MAME network output port "
                      << network_port << "." << std::endl;
            close_socket(network_listen);
            network_listen = invalid_socket();
            network_failed = true;
            return;
        }

        network_running = true;
        network_failed = false;
        std::cout << "MAME network outputs enabled on TCP port " << network_port << "." << std::endl;
    }

    void accept_network_clients()
    {
        while (true)
        {
            sockaddr_in remote {};
#ifdef _WIN32
            int length = sizeof(remote);
#else
            socklen_t length = sizeof(remote);
#endif
            socket_type client = accept(network_listen, reinterpret_cast<sockaddr*>(&remote), &length);
            if (client == invalid_socket())
            {
                if (!socket_would_block())
                    std::cerr << "External outputs: network accept failed." << std::endl;
                break;
            }

            set_nonblocking(client);
            network_clients.push_back(client);
            send_network_state(client);
        }
    }

    bool send_network(socket_type client, const std::string& text)
    {
#ifdef _WIN32
        const int result = send(client, text.data(), static_cast<int>(text.size()), 0);
#else
        const int result = static_cast<int>(send(client, text.data(), text.size(), MSG_NOSIGNAL));
#endif
        if (result >= 0)
            return true;
        return socket_would_block();
    }

    void send_network_state(socket_type client)
    {
        if (!send_network(client, std::string("mame_start = ") + MACHINE_NAME + "\r"))
            return;

        for (const auto& item : items)
            send_network(client, std::string(item.name) + " = " + std::to_string(item.value) + "\r");
    }

    void broadcast_network(const char* name, int value)
    {
        const std::string message = std::string(name) + " = " + std::to_string(value) + "\r";

        for (std::size_t i = 0; i < network_clients.size(); )
        {
            if (send_network(network_clients[i], message))
            {
                ++i;
            }
            else
            {
                close_socket(network_clients[i]);
                network_clients.erase(network_clients.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
    }

    void shutdown_network()
    {
        // Even a failed Windows bind may have successfully initialised
        // Winsock, so do not return before the Winsock cleanup below.
#ifndef _WIN32
        if (!network_running && network_listen == invalid_socket() && network_clients.empty())
            return;
#endif

        const std::string stop_message = "mame_stop = 1\r";
        for (auto client : network_clients)
        {
            send_network(client, stop_message);
            close_socket(client);
        }
        network_clients.clear();

        close_socket(network_listen);
        network_listen = invalid_socket();
        network_running = false;

#ifdef _WIN32
        if (winsock_started)
        {
            WSACleanup();
            winsock_started = false;
        }
#endif
    }

#ifdef _WIN32
    struct RegisteredClient
    {
        HWND hwnd;
        LPARAM id;
    };

    std::vector<RegisteredClient> windows_clients;
    HWND window_handle;
    bool window_running;

    UINT om_mame_start;
    UINT om_mame_stop;
    UINT om_mame_update_state;
    UINT om_mame_register_client;
    UINT om_mame_unregister_client;
    UINT om_mame_get_id_string;

    static constexpr const TCHAR* OUTPUT_WINDOW_CLASS = TEXT("MAMEOutput");
    static constexpr const TCHAR* OUTPUT_WINDOW_NAME  = TEXT("MAMEOutput");

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        ExternalOutputs* self = reinterpret_cast<ExternalOutputs*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCT*>(lparam);
            self = reinterpret_cast<ExternalOutputs*>(create->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        if (self)
            return self->handle_window_message(hwnd, message, wparam, lparam);

        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    void ensure_windows(bool enabled)
    {
        if (!enabled)
        {
            shutdown_windows();
            return;
        }

        if (!window_running)
            init_windows();
    }

    void init_windows()
    {
        om_mame_start             = RegisterWindowMessage(TEXT("MAMEOutputStart"));
        om_mame_stop              = RegisterWindowMessage(TEXT("MAMEOutputStop"));
        om_mame_update_state      = RegisterWindowMessage(TEXT("MAMEOutputUpdateState"));
        om_mame_register_client   = RegisterWindowMessage(TEXT("MAMEOutputRegister"));
        om_mame_unregister_client = RegisterWindowMessage(TEXT("MAMEOutputUnregister"));
        om_mame_get_id_string     = RegisterWindowMessage(TEXT("MAMEOutputGetIDString"));

        WNDCLASS wc {};
        wc.lpfnWndProc = &ExternalOutputs::window_proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = OUTPUT_WINDOW_CLASS;

        if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            std::cerr << "External outputs: unable to register MAMEOutput window class." << std::endl;
            return;
        }

        window_handle = CreateWindowEx(
            0,
            OUTPUT_WINDOW_CLASS,
            OUTPUT_WINDOW_NAME,
            WS_OVERLAPPEDWINDOW,
            0, 0, 1, 1,
            nullptr,
            nullptr,
            GetModuleHandle(nullptr),
            this);

        if (!window_handle)
        {
            std::cerr << "External outputs: unable to create MAMEOutput window." << std::endl;
            return;
        }

        window_running = true;
        PostMessage(HWND_BROADCAST, om_mame_start, reinterpret_cast<WPARAM>(window_handle), 0);
        std::cout << "MAME Windows outputs enabled (machine: cannonball)." << std::endl;
    }

    void shutdown_windows()
    {
        if (!window_running)
            return;

        PostMessage(HWND_BROADCAST, om_mame_stop, reinterpret_cast<WPARAM>(window_handle), 0);
        windows_clients.clear();
        DestroyWindow(window_handle);
        window_handle = nullptr;
        window_running = false;
    }

    void pump_windows_messages()
    {
        MSG message;
        while (PeekMessage(&message, window_handle, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }

    LRESULT handle_window_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == om_mame_register_client)
        {
            HWND client_hwnd = reinterpret_cast<HWND>(wparam);
            for (auto& client : windows_clients)
            {
                if (client.id == lparam)
                {
                    client.hwnd = client_hwnd;
                    send_all_windows(client_hwnd);
                    return 1;
                }
            }

            windows_clients.push_back({ client_hwnd, lparam });
            send_all_windows(client_hwnd);
            return 0;
        }

        if (message == om_mame_unregister_client)
        {
            auto found = std::find_if(windows_clients.begin(), windows_clients.end(),
                [lparam](const RegisteredClient& client) { return client.id == lparam; });
            if (found == windows_clients.end())
                return 1;
            windows_clients.erase(found);
            return 0;
        }

        if (message == om_mame_get_id_string)
        {
            send_id_string(reinterpret_cast<HWND>(wparam), static_cast<std::uint32_t>(lparam));
            return 0;
        }

        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    const char* name_for_id(std::uint32_t id) const
    {
        if (id == 0)
            return MACHINE_NAME;
        if (id == 1)
            return "pause";

        for (const auto& item : items)
            if (item.id == id)
                return item.name;
        return "";
    }

    void send_id_string(HWND target, std::uint32_t id)
    {
        struct CopyDataIdString
        {
            std::uint32_t id;
            char string[1];
        };

        const char* name = name_for_id(id);
        const std::size_t name_length = std::strlen(name);
        // Match MAME's allocation convention, including any alignment padding
        // in the copydata_id_string structure.
        const std::size_t bytes = sizeof(CopyDataIdString) + name_length + 1;
        std::vector<std::uint8_t> buffer(bytes, 0);
        auto* payload = reinterpret_cast<CopyDataIdString*>(buffer.data());
        payload->id = id;
        std::memcpy(payload->string, name, name_length + 1);

        COPYDATASTRUCT copydata {};
        copydata.dwData = 1; // COPYDATA_MESSAGE_ID_STRING in MAME
        copydata.cbData = static_cast<DWORD>(bytes);
        copydata.lpData = payload;
        SendMessage(target, WM_COPYDATA, reinterpret_cast<WPARAM>(window_handle), reinterpret_cast<LPARAM>(&copydata));
    }

    void send_all_windows(HWND target)
    {
        for (const auto& item : items)
            PostMessage(target, om_mame_update_state, item.id, item.value);
    }

    void broadcast_windows(std::uint32_t id, int value)
    {
        for (const auto& client : windows_clients)
            PostMessage(client.hwnd, om_mame_update_state, id, value);
    }
#else
    void ensure_windows(bool) {}
#endif
};
