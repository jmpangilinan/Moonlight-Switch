//
//  add_host_tab.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#include "add_host_tab.hpp"
#include "DiscoverManager.hpp"
#include "helper.hpp"
#include "main_tabs_view.hpp"
#include <cstdio>

extern "C" {
#include "netbird.h"
}

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#if defined(PLATFORM_IOS) || defined(PLATFORM_TVOS) || defined(PLATFORM_VISIONOS)
extern void darwin_mdns_start(ServerCallback<std::vector<Host>>& callback);
extern void darwin_mdns_stop();
#endif

namespace {
std::string strip_ipv4_port(const std::string& address) {
    const auto firstColon = address.find(':');
    if (firstColon == std::string::npos) {
        return address;
    }

    if (address.find(':', firstColon + 1) != std::string::npos) {
        return address;
    }

    return address.substr(0, firstColon);
}

bool is_private_ipv4(const in_addr& address) {
    const uint32_t value = ntohl(address.s_addr);
    const uint8_t a = (value >> 24) & 0xFF;
    const uint8_t b = (value >> 16) & 0xFF;

    return a == 10 || a == 127 || (a == 169 && b == 254) ||
           (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
}

bool should_store_manual_address_as_remote(const std::string& address) {
    const auto hostPart = strip_ipv4_port(address);
    if (hostPart.empty()) {
        return false;
    }

    in_addr parsed{};
    if (inet_pton(AF_INET, hostPart.c_str(), &parsed) != 1) {
        return false;
    }

    return !is_private_ipv4(parsed);
}
}

AddHostTab::AddHostTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/add_host.xml");

    hostIP->init("add_host/host_ip"_i18n, "");
    hostIP->setPlaceholder("192.168.1.109:47989");
    hostIP->setHint("192.168.1.109:47989");

    connect->setText("add_host/connect"_i18n);
    connect->registerClickAction([this](View* view) {
        Host host;
        const auto inputAddress = hostIP->getValue();
        if (should_store_manual_address_as_remote(inputAddress)) {
            host.remoteAddress = inputAddress;
        } else {
            host.address = inputAddress;
        }
        connectHost(host);
        return true;
    });

    if (GameStreamClient::can_find_host())
        findHost();
    else {
        searchHeader->setTitle("add_host/search_error"_i18n);
        loader->setVisibility(brls::Visibility::GONE);
    }

    // NetBird: show VPN peers in search results (not persistent host list)
    addNetbirdPeers();

    registerAction("add_host/search_refresh"_i18n, ControllerButton::BUTTON_X,
                   [this](View* view) {
#ifdef MULTICAST_DISABLED
                       DiscoverManager::instance().reset();
#endif
                       findHost();
                       addNetbirdPeers();
                       return true;
                   });
    setActionAvailable(BUTTON_X, GameStreamClient::can_find_host());
}

void AddHostTab::addNetbirdPeers() {
    // Auto-discovers NetBird VPN peers in the search results only.
    // Probes each peer on port 47989 (Sunshine HTTP) and only shows
    // ones that respond. Runs async to avoid blocking the UI.
    if (!netbird_is_ready()) return;
    
    int count = netbird_get_peer_count();
    // The probe thread and its queued sync lambdas can outlive the tab
    // (user navigates away mid-probe) — `alive` guards every use of `this`.
    brls::async([this, count, alive = this->alive]() {
        for (int i = 0; i < count; i++) {
            if (!alive->load()) return;

            char ip[64], name[128];
            if (!netbird_get_peer(i, ip, sizeof(ip), name, sizeof(name))) continue;

            // Quick probe — 300ms timeout. Sunshine responds quickly
            // because WG sessions are already established via passive handshake.
            if (!netbird_peer_reachable(ip, 47989, 300)) continue;

            // Found — add to search results on main thread
            brls::sync([this, alive, ip_str = std::string(ip), name_str = std::string(name)]() {
                if (!alive->load()) return;
                if (searchBoxIpExists(ip_str)) return;
                
                char mac[32];
                unsigned h = 0;
                for (const char* p = ip_str.c_str(); *p; p++) h = h * 31 + (unsigned char)*p;
                snprintf(mac, sizeof(mac), "00:00:%02X:%02X:%02X:%02X",
                    (h>>24)&0xFF, (h>>16)&0xFF, (h>>8)&0xFF, h&0xFF);
                Host hst;
                hst.address = "127.0.0.1";
                hst.remoteAddress = ip_str;
                hst.hostname = std::string("NetBird: ") + name_str;
                hst.mac = mac;
                
                auto hostButton = new brls::DetailCell();
                hostButton->setText(hst.hostname);
                hostButton->setDetailText(ip_str);
                hostButton->setDetailTextColor(
                    brls::Application::getTheme()["brls/text_disabled"]);
                hostButton->registerClickAction([this, hst](View* view) {
                    connectHost(hst);
                    return true;
                });
                searchBox->addView(hostButton);
            });
        }
    });
}

void AddHostTab::fillSearchBox(const GSResult<std::vector<Host>>& hostsRes) {
    loader->setVisibility(DiscoverManager::instance().isPaused()
                              ? brls::Visibility::GONE
                              : brls::Visibility::VISIBLE);

    if (hostsRes.isSuccess()) {
        appendSearchHosts(hostsRes.value());
    } else {
        loader->setVisibility(brls::Visibility::GONE);
        searchHeader->setTitle("add_host/search"_i18n + " - " +
                               hostsRes.error());
    }
}

void AddHostTab::appendSearchHosts(const std::vector<Host>& hosts) {
    for (const Host& host : hosts) {
        const auto displayAddress = host.preferred_address();
        if (displayAddress.empty() || searchBoxIpExists(displayAddress))
            continue;

        auto hostButton = new brls::DetailCell();
        hostButton->setText(host.hostname);
        hostButton->setDetailText(displayAddress);
        hostButton->setDetailTextColor(
            brls::Application::getTheme()["brls/text_disabled"]);
        hostButton->registerClickAction([this, host](View* view) {
            connectHost(host);
            return true;
        });
        searchBox->addView(hostButton);
    }
}

bool AddHostTab::searchBoxIpExists(const std::string& ip) {
    return std::any_of(searchBox->getChildren().begin(), searchBox->getChildren().end(), [ip](View* child) {
        auto cell = dynamic_cast<DetailCell*>(child);
        return cell && cell->detail->getFullText() == ip;
    });
}

void AddHostTab::findHost() {
#ifdef MULTICAST_DISABLED
    DiscoverManager::instance().start();
    fillSearchBox(DiscoverManager::instance().getHosts());
    DiscoverManager::instance().getHostsUpdateEvent()->unsubscribe(
        searchSubscription);
    searchSubscription =
        DiscoverManager::instance().getHostsUpdateEvent()->subscribe(
            [this](auto result) { fillSearchBox(result); });
#else
    stopSearchHost();
    const uint64_t generation = searchGeneration;
    searchBox->clearViews();
    searchHeader->setTitle("add_host/search"_i18n);
    loader->setVisibility(brls::Visibility::VISIBLE);
    ASYNC_RETAIN
#if defined(PLATFORM_IOS) || defined(PLATFORM_TVOS) || defined(PLATFORM_VISIONOS)
    darwin_mdns_start(
#else
    GameStreamClient::find_hosts(
#endif
        [ASYNC_TOKEN, generation](const GSResult<std::vector<Host>>& result) {
            ASYNC_RELEASE

            if (generation != searchGeneration) {
                return;
            }

            if (result.isSuccess()) {
                appendSearchHosts(result.value());
            } else {
                loader->setVisibility(brls::Visibility::GONE);
                showError(result.error(), [] {});
            }
        });
#endif
}

void AddHostTab::stopSearchHost() {
    searchGeneration++;
#ifdef PLATFORM_IOS
#elif defined(PLATFORM_TVOS) || defined(PLATFORM_VISIONOS)
#else
    GameStreamClient::cancel_find_hosts();
#endif
}

void AddHostTab::connectHost(const Host& host) {
    fprintf(stderr, "[ML-NB] connectHost: peer %s remote=%s\n",
            host.address.c_str(), host.remoteAddress.c_str());
    
    // NetBird peer: restart proxy for the selected peer before connecting.
    // The proxy bridges bsd sockets → lwIP → WireGuard and can only route
    // to one peer at a time, so we switch the target per-connection.
    // Accept either the explicit proxy form (address="127.0.0.1") or a
    // manually-entered VPN IP that matches a known peer.
    Host effective_host = host;
    const char *target_peer = NULL;
    if (host.address == "127.0.0.1" && !host.remoteAddress.empty()) {
        target_peer = host.remoteAddress.c_str();
    } else if (netbird_is_ready() && (!host.remoteAddress.empty() || !host.address.empty())) {
        // User typed a VPN IP manually → check if it's a known peer
        const std::string& candidate = !host.remoteAddress.empty() ? host.remoteAddress : host.address;
        int count = netbird_get_peer_count();
        for (int i = 0; i < count; i++) {
            char ip[64], name[128];
            if (netbird_get_peer(i, ip, sizeof(ip), name, sizeof(name))) {
                if (candidate == ip) { target_peer = ip; break; }
            }
        }
    }
    if (target_peer) {
        fprintf(stderr, "[ML-NB] switching proxy to %s\n", target_peer);
        netbird_proxy_stop();  // stops TCP + UDP
        netbird_proxy_start(target_peer, 47989);
        netbird_proxy_start_udp(target_peer);
        effective_host.address = "127.0.0.1";
        effective_host.remoteAddress = target_peer;
    }
    
    pauseSearching();

    Dialog* loaderView = createLoadingDialog("add_host/try_connect"_i18n);
    loaderView->open();

    fprintf(stderr, "[ML-NB] calling GameStreamClient::connect()...\n");
    GameStreamClient::instance().connect(
        effective_host, [this, loaderView, effective_host](const GSResult<SERVER_DATA>& result) {
            fprintf(stderr, "[ML-NB] connect callback: success=%d\n", result.isSuccess());
            loaderView->close([this, result, effective_host] {
                if (result.isSuccess()) {
                    Host pairedHost = effective_host;
                    pairedHost.hostname = result.value().hostname;
                    pairedHost.mac = result.value().mac;

                    if (result.value().paired) {
                        showAlert("add_host/paired_error"_i18n, [pairedHost] {
                            Settings::instance().add_host(pairedHost);
                            MainTabs::getInstanse()->refillTabs();
                        });

                        return;
                    }

                    auto pin = fmt::format("{}{}{}{}", (int)rand() % 10, (int)rand() % 10,
                            (int)rand() % 10, (int)rand() % 10);

                    brls::Dialog* dialog = createLoadingDialog(
                        "add_host/pair_prefix"_i18n + pin +
                        "add_host/pair_postfix"_i18n);
                    dialog->setCancelable(false);
                    dialog->open();

                    ASYNC_RETAIN
                    GameStreamClient::instance().pair(
                        pairedHost, pin,
                        [ASYNC_TOKEN, pairedHost, dialog](const GSResult<bool>& result) {
                            ASYNC_RELEASE
                            dialog->dismiss([result, pairedHost] {
                                if (result.isSuccess()) {
                                    Settings::instance().add_host(pairedHost);
                                    MainTabs::getInstanse()->refillTabs();
                                    AddHostTab::startSearching();
                                } else {
                                    showError(result.error(), [] {
                                        AddHostTab::startSearching();
                                    });
                                }
                            });
                        });
                } else {
                    showError(result.error(),
                              [] { AddHostTab::startSearching(); });
                }
            });
        });
}

void AddHostTab::pauseSearching() {
#ifdef MULTICAST_DISABLED
    DiscoverManager::instance().pause();
#endif
}

void AddHostTab::startSearching() {
#ifdef MULTICAST_DISABLED
    DiscoverManager::instance().start();
#endif
}

AddHostTab::~AddHostTab() {
    alive->store(false);
    stopSearchHost();
#ifdef MULTICAST_DISABLED
    DiscoverManager::instance().pause();
    DiscoverManager::instance().getHostsUpdateEvent()->unsubscribe(
        searchSubscription);
#elif defined(PLATFORM_IOS) || defined(PLATFORM_TVOS) || defined(PLATFORM_VISIONOS)
    darwin_mdns_stop();
#endif
}

brls::View* AddHostTab::create() {
    // Called by the XML engine to create a new AddHostTab
    return new AddHostTab();
}
