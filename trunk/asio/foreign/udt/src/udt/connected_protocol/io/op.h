#ifndef UDT_CONNECTED_PROTOCOL_IO_OP_H_
#define UDT_CONNECTED_PROTOCOL_IO_OP_H_

#include <boost/asio/detail/handler_tracking.hpp>

#include <boost/system/error_code.hpp>


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace io {

/// Base class for pending io operations without size information
class basic_pending_io_operation BOOST_ASIO_INHERIT_TRACKED_HANDLER {
 public:
  /// Function called on completion
  /**
  * @param ec The error code resulting of the completed operation
  */
  void complete(const boost::system::error_code& ec) {
    auto destroy = false;
    func_(this, destroy, ec);
  }

  void destroy() {
    auto destroy = true;
    func_(this, destroy, boost::system::error_code());
  }

 protected:
  using func_type = void(*)(basic_pending_io_operation*, bool,
                            const boost::system::error_code& ec);

  basic_pending_io_operation(func_type func) : next_(0), func_(func) {}

  ~basic_pending_io_operation() {}

  friend class boost::asio::detail::op_queue_access;
  basic_pending_io_operation* next_;
  func_type func_;
};

/// Base class for pending io operations with size information
class basic_pending_sized_io_operation BOOST_ASIO_INHERIT_TRACKED_HANDLER {
 public:
  /// Function called on completion
  /**
  * @param ec The error code resulting of the completed operation
  * @param length The length of the result
  */
  void complete(const boost::system::error_code& ec, std::size_t length) {
    auto destroy = false;
    func_(this, destroy, ec, length);
  }

  void destroy() {
    auto destroy = true;
    func_(this, destroy, boost::system::error_code(), 0);
  }

 protected:
  using func_type = void (*)(basic_pending_sized_io_operation*, bool,
                             const boost::system::error_code& ec,
                             std::size_t length);

  basic_pending_sized_io_operation(func_type func) : next_(0), func_(func) {}

  ~basic_pending_sized_io_operation() {}

  friend class boost::asio::detail::op_queue_access;
  basic_pending_sized_io_operation* next_;
  func_type func_;
};

}  // io
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_IO_OP_H_
