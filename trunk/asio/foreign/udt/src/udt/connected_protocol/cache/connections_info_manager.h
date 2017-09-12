#ifndef UDT_CONNECTED_PROTOCOL_CACHE_CONNECTIONS_INFO_MANAGER_H_
#define UDT_CONNECTED_PROTOCOL_CACHE_CONNECTIONS_INFO_MANAGER_H_

#include <cstdint>

#include <atomic>
#include <map>
#include <string>

#include <mutex>
#include "../../../../../../../misc/sync.h"

#include "connection_info.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace cache {

template <class Protocol>
class ConnectionsInfoManager {
 public:
  using NextEndpoint = typename Protocol::next_layer_protocol::endpoint;

 private:
  using RemoteAddress = std::string;
  using ConnectionsInfoMap = std::map<RemoteAddress, ConnectionInfo::Ptr>;

 public:
  using Endpoint = typename Protocol::endpoint;

 public:
  ConnectionsInfoManager(uint32_t max_cache_size = 64)
      : max_cache_size_(max_cache_size),
        connections_mutex_(),
        connections_info_() {}

  std::weak_ptr<ConnectionInfo> GetConnectionInfo(const NextEndpoint& next_endpoint) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_connections(connections_mutex_);
    std::string address(next_endpoint.address().to_string());
    ConnectionsInfoMap::iterator connection_info_it(
        connections_info_.find(address));
    if (connection_info_it != connections_info_.end()) {
      return connection_info_it->second;
    }

    if (connections_info_.size() > max_cache_size_) {
      FreeItem();
    }

    ConnectionInfo::Ptr p_connection_info(std::make_shared<ConnectionInfo>());
    connections_info_[address] = p_connection_info;

    return p_connection_info;
  }

 private:
  void FreeItem() {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock_connections(connections_mutex_);
    ConnectionsInfoMap::iterator oldest_pair_it(connections_info_.begin());
    ConnectionsInfoMap::iterator current_pair_it(oldest_pair_it);
    ConnectionsInfoMap::iterator end_pair_it(connections_info_.end());
    while (current_pair_it != end_pair_it) {
      if (current_pair_it->second < oldest_pair_it->second) {
        oldest_pair_it = current_pair_it;
      }
      ++current_pair_it;
    }

    connections_info_.erase(oldest_pair_it);
  }

 private:
  uint32_t max_cache_size_;
  synapse::misc::recursive_mutex connections_mutex_;
  ConnectionsInfoMap connections_info_;
};

}  // congestion
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_CACHE_CONNECTIONS_INFO_MANAGER_H_
