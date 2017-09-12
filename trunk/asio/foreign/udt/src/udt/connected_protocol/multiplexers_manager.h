#ifndef UDT_CONNECTED_PROTOCOL_MULTIPLEXERS_MANAGER_H_
#define UDT_CONNECTED_PROTOCOL_MULTIPLEXERS_MANAGER_H_

#include <map>
#include <memory>

#include <mutex>
#include "../../../../../../misc/sync.h"

#include "multiplexer.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class Protocol>
class MultiplexerManager {
 public:
  using NextSocket = typename Protocol::next_layer_protocol::socket;
  using NextLayerEndpoint = typename Protocol::next_layer_protocol::endpoint;

 private:
  using MultiplexerPtr = typename Multiplexer<Protocol>::Ptr;
  using MultiplexersMap = std::map<NextLayerEndpoint, MultiplexerPtr>;

  // TODO move multiplexers management in service
 public:
  MultiplexerManager() : mutex_(), multiplexers_() {}
  
	template <typename Callback>
  MultiplexerPtr GetMultiplexer(boost::asio::io_service &io_service,
                                   const NextLayerEndpoint &next_local_endpoint,
                                   boost::system::error_code &ec,
																	 Callback && cbk) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    auto multiplexer_it = multiplexers_.find(next_local_endpoint);
    if (multiplexer_it == multiplexers_.end()) {
      NextSocket next_layer_socket(io_service);
      next_layer_socket.open(next_local_endpoint.protocol());
			cbk(next_layer_socket);
      // Empty endpoint will bind the socket to an available port
      next_layer_socket.bind(next_local_endpoint, ec);
      if (ec) {
        return nullptr;
      }

      MultiplexerPtr p_multiplexer =
          Multiplexer<Protocol>::Create(this, std::move(next_layer_socket));

      boost::system::error_code ec;

      multiplexers_[p_multiplexer->local_endpoint(ec)] = p_multiplexer;
      p_multiplexer->Start();

      return p_multiplexer;
    }

    return multiplexer_it->second;
  }

  void CleanMultiplexer(const NextLayerEndpoint &next_local_endpoint) {
    ::std::lock_guard<synapse::misc::mutex> lock(mutex_);
    if (multiplexers_.find(next_local_endpoint) != multiplexers_.end()) {
      boost::system::error_code ec;
			::std::cerr << "cleaing multiplexers" << ::std::endl;
      multiplexers_[next_local_endpoint]->Stop(ec);
      multiplexers_.erase(next_local_endpoint);
      // @todo should ec be swallowed here?
    }
  }

 private:
  synapse::misc::mutex mutex_;
  MultiplexersMap multiplexers_;
};

}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_MULTIPLEXERS_MANAGER_H_
