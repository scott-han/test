#ifndef UDT_CONNECTED_PROTOCOL_RESOLVER_H_
#define UDT_CONNECTED_PROTOCOL_RESOLVER_H_

#include <cstdint>

#include <vector>

#include <boost/asio/io_service.hpp>

#include <boost/system/error_code.hpp>

#include "../common/error/error.h"

#include "resolver_query.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class Protocol>
class Resolver {
 private:
  class EndpointIterator {
   public:
    EndpointIterator() : index_(-1) {}
    EndpointIterator(std::vector<typename Protocol::endpoint> endpoints)
        : endpoints_(endpoints), index_(0) {}

		bool 
		operator != (EndpointIterator && rhs) const 
		{
			return index_ != rhs.index_ || endpoints_ != rhs.endpoints_ ? true : false;
		}

    typename Protocol::endpoint& operator*() { return endpoints_[index_]; }
    typename Protocol::endpoint* operator->() { return &endpoints_[index_]; }

    typename Protocol::endpoint& operator++() {
      ++index_;
      return endpoints_[index_];
    }

    typename Protocol::endpoint operator++(int) {
      ++index_;
      return endpoints_[index_ - 1];
    }

    typename Protocol::endpoint& operator--() {
      --index_;
      return endpoints_[index_];
    }

    typename Protocol::endpoint operator--(int) {
      --index_;
      return endpoints_[index_ + 1];
    }

   private:
    std::vector<typename Protocol::endpoint> endpoints_;
    std::size_t index_;
  };

  using NextLayer = typename Protocol::next_layer_protocol;
  using NextLayerEndpoint = typename NextLayer::endpoint;

 public:
  using protocol_type = Protocol;
  using endpoint_type = typename Protocol::endpoint;
  using query = ResolverQuery<Protocol>;
  using iterator = EndpointIterator;

 public:
  Resolver(boost::asio::io_service& io_service) : io_service_(io_service) {}

  iterator resolve(const query& q, boost::system::error_code& ec) {
    typename NextLayer::resolver next_layer_resolver(io_service_);
    auto next_layer_iterator =
        next_layer_resolver.resolve(q.next_layer_query(), ec);
    if (ec) {
      return iterator();
    }

    std::vector<endpoint_type> result;
    result.emplace_back(q.socket_id(), NextLayerEndpoint(*next_layer_iterator));
    ec.assign(udt::common::error::success, udt::common::error::get_error_category());

    return iterator(result);
  }

  iterator resolve(query const & q) {
		::boost::system::error_code ec;
    auto rv(resolve(q, ec));
    if (ec)
			throw ::boost::system::system_error(ec);
    return rv;
  }


 private:
  boost::asio::io_service& io_service_;
};

}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_RESOLVER_H_
