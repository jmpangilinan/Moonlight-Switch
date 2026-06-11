//
//  settings_netbird_tab.cpp — programmatic, no XML
//
#include "settings_netbird_tab.hpp"
#include "Settings.hpp"
#include <borealis.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/header.hpp>

class brls::Dialog;
brls::Dialog* createLoadingDialog(const std::string& text,
                                   const std::function<void(void)>& onCancel = nullptr);

extern "C" {
#include "netbird.h"
}

NetbirdSettingsTab::NetbirdSettingsTab() {
    // Build UI programmatically
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setGrow(1.0f);
    
    brls::Box* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setWidth(10000);
    content->setHeight(brls::View::AUTO);
    
    // Header
    brls::Header* header = new brls::Header();
    header->setTitle("NetBird VPN");
    content->addView(header);
    
    // Connect button
    connect_btn = new brls::BooleanCell();
    connect_btn->init("Connect to VPN", false,
        [this](bool value) {
            if (value) connectVPN();
            else disconnectVPN();
        });
    content->addView(connect_btn);
    
    // Server input
    server_input = new brls::InputCell();
    server_input->init("Server URL", Settings::instance().netbird_server(),
        [this](const std::string& text) {
            Settings::instance().set_netbird_server(text);
            Settings::instance().save();
        },
        "https://netbird.example.com:9443",
        "Management server URL",
        256
    );
    content->addView(server_input);
    
    // Key input
    key_input = new brls::InputCell();
    key_input->init("Setup Key", Settings::instance().netbird_key(),
        [this](const std::string& text) {
            Settings::instance().set_netbird_key(text);
            Settings::instance().save();
        },
        "",
        "Your NetBird setup key",
        256
    );
    content->addView(key_input);
    
    // Status card — Switch-style colored box
    status_card = new brls::Box();
    status_card->setAxis(brls::Axis::COLUMN);
    status_card->setPadding(20, 16, 20, 16);
    status_card->setCornerRadius(8);
    status_card->setMargins(16, 8, 16, 8);
    status_card->setAlignItems(brls::AlignItems::CENTER);
    status_card->setBackgroundColor(nvgRGB(50, 50, 55));
    
    status_label = new brls::Label();
    status_label->setFontSize(22);
    status_label->setText("Disconnected");
    status_label->setTextColor(nvgRGB(180, 180, 185));
    status_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    status_card->addView(status_label);
    
    status_detail = new brls::Label();
    status_detail->setFontSize(15);
    status_detail->setTextColor(nvgRGB(140, 140, 145));
    status_detail->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    status_card->addView(status_detail);
    
    status_peers = new brls::Label();
    status_peers->setFontSize(14);
    status_peers->setTextColor(nvgRGB(120, 120, 125));
    status_peers->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    status_card->addView(status_peers);
    
    content->addView(status_card);
    
    scroll->setContentView(content);
    this->addView(scroll);
    
    // Restore state if already connected
    if (netbird_is_ready()) {
        connect_btn->setOn(true, false);
        status_label->setText("Connected");
        status_label->setTextColor(nvgRGB(130, 220, 140));
        status_card->setBackgroundColor(nvgRGB(30, 55, 40));
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", netbird_get_ip() ? netbird_get_ip() : "?");
        status_detail->setText(buf);
        status_detail->setTextColor(nvgRGB(180, 200, 185));
        snprintf(buf, sizeof(buf), "%d peers", netbird_get_peer_count());
        status_peers->setText(buf);
        status_peers->setTextColor(nvgRGB(150, 180, 160));
    }
}

NetbirdSettingsTab::~NetbirdSettingsTab() {}

brls::View* NetbirdSettingsTab::create() {
    return new NetbirdSettingsTab();
}

void NetbirdSettingsTab::connectVPN() {
    auto& s = Settings::instance();
    if (s.netbird_server().empty() || s.netbird_key().empty()) {
        status_label->setText("Configure server + key first");
        return;
    }
    status_label->setText("Connecting...");
    status_label->setTextColor(nvgRGB(255, 255, 255));
    status_detail->setText("Authenticating...");
    status_peers->setText("");
    status_card->setBackgroundColor(nvgRGB(60, 65, 75));
    connect_btn->setEnabled(false);
    
    brls::Dialog* loading = createLoadingDialog("Connecting to VPN...");
    loading->setCancelable(false);
    loading->open();
    
    brls::async([this, loading]() {
        char err[256];
        int ret = netbird_init(
            Settings::instance().netbird_server().c_str(),
            Settings::instance().netbird_key().c_str(),
            err, sizeof(err)
        );
        brls::sync([this, ret, err, loading]() {
            loading->close();
            if (ret == 0) {
                const char *ip = netbird_get_ip();
                int peer_count = netbird_get_peer_count();
                status_label->setText("Connected");
                status_label->setTextColor(nvgRGB(130, 220, 140));
                status_card->setBackgroundColor(nvgRGB(30, 55, 40));
                char buf[64];
                snprintf(buf, sizeof(buf), "%s", ip ? ip : "?");
                status_detail->setText(buf);
                status_detail->setTextColor(nvgRGB(180, 200, 185));
                snprintf(buf, sizeof(buf), "%d peers", peer_count);
                status_peers->setText(buf);
                status_peers->setTextColor(nvgRGB(150, 180, 160));
                connect_btn->setOn(true, false);
                // Proxy is started on-demand in connectHost() when
                // user clicks a peer from the search results.
            } else {
                connect_btn->setOn(false, true);
                connect_btn->setEnabled(true);
                status_label->setText("Connection Failed");
                status_label->setTextColor(nvgRGB(240, 130, 130));
                status_card->setBackgroundColor(nvgRGB(55, 30, 30));
                status_detail->setText(err);
                status_detail->setTextColor(nvgRGB(200, 150, 150));
            }
        });
    });
}

void NetbirdSettingsTab::disconnectVPN() {
    netbird_shutdown();
    status_label->setText("Disconnected");
    status_label->setTextColor(nvgRGB(180, 180, 185));
    status_card->setBackgroundColor(nvgRGB(50, 50, 55));
    status_detail->setText("");
    status_peers->setText("");
    connect_btn->setOn(false, false);
}
