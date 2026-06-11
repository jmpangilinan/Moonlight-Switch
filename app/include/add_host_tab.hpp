//
//  add_host_tab.hpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <borealis.hpp>
#include "Settings.hpp"
#include "GameStreamClient.hpp"

class AddHostTab : public brls::Box
{
  public:
    AddHostTab();
    ~AddHostTab() override;

    static brls::View* create();

  private:
    void findHost();
    void addNetbirdPeers();
    void stopSearchHost();
    void connectHost(const Host& host);
    void fillSearchBox(const GSResult<std::vector<Host>>& hostsRes);
    void appendSearchHosts(const std::vector<Host>& hosts);
    static void pauseSearching();
    static void startSearching();
    brls::Event<GSResult<std::vector<Host>>>::Subscription searchSubscription;
    uint64_t searchGeneration = 0;

    // Outlives the tab; dtor flips it so the NetBird peer-probe thread and
    // its queued brls::sync lambdas never touch a destroyed `this`.
    std::shared_ptr<std::atomic<bool>> alive =
        std::make_shared<std::atomic<bool>>(true);

    bool searchBoxIpExists(const std::string& ip);
    
    BRLS_BIND(brls::InputCell, hostIP, "hostIP");
    BRLS_BIND(brls::DetailCell, connect, "connect");
    BRLS_BIND(brls::Box, searchBox, "search_box");
    BRLS_BIND(brls::Box, loader, "loader");
    BRLS_BIND(brls::Header, searchHeader, "search_header");
};
