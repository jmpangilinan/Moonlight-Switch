//
//  settings_netbird_tab.hpp
//
#pragma once

#include <borealis.hpp>

class NetbirdSettingsTab : public brls::Box {
public:
    NetbirdSettingsTab();
    ~NetbirdSettingsTab();
    static brls::View* create();

private:
    void connectVPN();
    void disconnectVPN();
    
    brls::BooleanCell* connect_btn = nullptr;
    brls::InputCell* server_input = nullptr;
    brls::InputCell* key_input = nullptr;
    brls::Box* status_card = nullptr;
    brls::Label* status_label = nullptr;
    brls::Label* status_detail = nullptr;
    brls::Label* status_peers = nullptr;
};
