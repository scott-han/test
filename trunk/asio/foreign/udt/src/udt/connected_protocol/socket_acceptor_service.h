#ifndef UDT_CONNECTED_PROTOCOL_SOCKET_ACCEPTOR_SERVICE_H_
#define UDT_CONNECTED_PROTOCOL_SOCKET_ACCEPTOR_SERVICE_H_

#include <boost/asio/async_result.hpp>
#include <boost/asio/io_service.hpp>


#include <boost/system/error_code.hpp>

#include "../common/error/error.h"

#include "io/accept_op.h"

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {

template <class Prococol>
class socket_acceptor_service : public boost::asio::detail::service_base<
                                    socket_acceptor_service<Prococol>> {
 public:
  using protocol_type = Prococol;
  using endpoint_type = typename protocol_type::endpoint;
  using p_endpoint_type = std::shared_ptr<endpoint_type>;
  using resolver_type = typename protocol_type::resolver;

  using next_socket_type = typename protocol_type::next_layer_protocol::socket;
  using p_next_socket_type = std::shared_ptr<next_socket_type>;
  using acceptor_session_type = typename protocol_type::acceptor_session;
  using p_acceptor_session_type = std::shared_ptr<acceptor_session_type>;
  using multiplexer = typename protocol_type::multiplexer;
  using p_multiplexer_type = std::shared_ptr<multiplexer>;

  struct implementation_type {
    p_multiplexer_type p_multiplexer;
    p_acceptor_session_type p_acceptor;
  };

  using native_handle_type = implementation_type&;
  using native_type = native_handle_type;

 public:
  explicit socket_acceptor_service(boost::asio::io_service& io_service)
      : boost::asio::detail::service_base<socket_acceptor_service>(io_service) {
  }

  virtual ~socket_acceptor_service() {}

  void construct(implementation_type& impl) {
    impl.p_multiplexer.reset();
    impl.p_acceptor.reset();
  }

  void destroy(implementation_type& impl) {
    impl.p_multiplexer.reset();
    impl.p_acceptor.reset();
  }

  void move_construct(implementation_type& impl, implementation_type& other) {
    impl = std::move(other);
  }

  void move_assign(implementation_type& impl, implementation_type& other) {
    impl = std::move(other);
  }

  boost::system::error_code open(implementation_type& impl,
                                 const protocol_type& ,
                                 boost::system::error_code& ec) {
    if (!impl.p_acceptor) {
      impl.p_acceptor = std::make_shared<acceptor_session_type>();
      ec.assign(udt::common::error::success,
                udt::common::error::get_error_category());
    } else {
      ec.assign(udt::common::error::device_or_resource_busy,
                udt::common::error::get_error_category());
    }
    return ec;
  }

  bool is_open(const implementation_type& impl) const {
    return impl.p_acceptor != nullptr;
  }

  endpoint_type local_endpoint(const implementation_type& impl,
                               boost::system::error_code& ec) const {
    if (!is_open(impl)) {
      ec.assign(udt::common::error::broken_pipe,
                udt::common::error::get_error_category());

      return endpoint_type();
    }

    ec.assign(udt::common::error::success, udt::common::error::get_error_category());
    return endpoint_type(0, impl.p_acceptor->next_local_endpoint(ec));
  }

  boost::system::error_code close(implementation_type& impl,
                                  boost::system::error_code& ec) {
    if (!is_open(impl)) {
      ec.assign(udt::common::error::broken_pipe,
                udt::common::error::get_error_category());
      return ec;
    }

    impl.p_acceptor->Close();
    impl.p_multiplexer->RemoveAcceptor();
    impl.p_acceptor.reset();
    impl.p_multiplexer.reset();

    return ec;
  }

  native_type native(implementation_type& impl) { return impl; }

  native_handle_type native_handle(implementation_type& impl) { return impl; }

  boost::system::error_code 
	set_option(implementation_type& , ::boost::asio::socket_base::receive_buffer_size const & option, boost::system::error_code& ec) 
	{
		so_rcvbuf = option;
		ec.assign(udt::common::error::success, udt::common::error::get_error_category());
    return ec;
  }

  boost::system::error_code 
	set_option(implementation_type& , ::boost::asio::socket_base::send_buffer_size const & option, boost::system::error_code& ec) 
	{
		so_sndbuf = option;
		ec.assign(udt::common::error::success, udt::common::error::get_error_category());
    return ec;
  }

  template <typename SettableSocketOption>
  boost::system::error_code set_option(implementation_type& ,
                                       const SettableSocketOption& ,
                                       boost::system::error_code& ec) {
    ec.assign(udt::common::error::function_not_supported,
              udt::common::error::get_error_category());
    return ec;
  }

  template <typename GettableSocketOption>
  boost::system::error_code get_option(const implementation_type& impl,
                                       GettableSocketOption& option,
                                       boost::system::error_code& ec) const {
    ec.assign(udt::common::error::function_not_supported,
              udt::common::error::get_error_category());
    return ec;
  }

  template <typename IoControlCommand>
  boost::system::error_code io_control(implementation_type& impl,
                                       IoControlCommand& command,
                                       boost::system::error_code& ec) {
    ec.assign(udt::common::error::function_not_supported,
              udt::common::error::get_error_category());
    return ec;
  }

	::boost::asio::socket_base::receive_buffer_size so_rcvbuf{-1};
	::boost::asio::socket_base::send_buffer_size so_sndbuf{-1};

  boost::system::error_code bind(implementation_type& impl,
                                 const endpoint_type& endpoint,
                                 boost::system::error_code& ec) {
    if (impl.p_multiplexer) {
      ec.assign(udt::common::error::device_or_resource_busy,
                udt::common::error::get_error_category());

      return ec;
    }

    impl.p_multiplexer = protocol_type::multiplexers_manager_.GetMultiplexer(this->get_io_service(), endpoint.next_layer_endpoint(), ec, 
			[this](typename protocol_type::next_socket_type & s){
				if (so_rcvbuf.value() != static_cast<decltype(so_rcvbuf.value())>(-1))
					s.set_option(so_rcvbuf);
				if (so_sndbuf.value() != static_cast<decltype(so_sndbuf.value())>(-1))
					s.set_option(so_sndbuf);
			}
		);
    if (ec) {
      return ec;
    }

    impl.p_multiplexer->SetAcceptor(ec, impl.p_acceptor);

    return ec;
  }

  boost::system::error_code listen(implementation_type& impl, int backlog,
                                   boost::system::error_code& ec) {
    impl.p_acceptor->Listen(backlog, ec);
    return ec;
  }

  template <typename Protocol1, typename SocketService>
  boost::system::error_code accept(
      implementation_type& impl,
      boost::asio::basic_socket<Protocol1, SocketService>& peer,
      endpoint_type* p_peer_endpoint, boost::system::error_code& ec,
      typename std::enable_if<::std::is_convertible<
          protocol_type, Protocol1>::value>::type* = 0) {
    try {
      ec.clear();
      auto future_value =
          async_accept(impl, peer, p_peer_endpoint, boost::asio::use_future);
      future_value.get();
      ec.assign(udt::common::error::success,
                udt::common::error::get_error_category());
    } catch (const std::system_error& e) {
      ec.assign(e.code().value(), udt::common::error::get_error_category());
    }
    return ec;
  }

  template <typename Protocol1, typename SocketService, typename AcceptHandler>
  BOOST_ASIO_INITFN_RESULT_TYPE(AcceptHandler, void(boost::system::error_code))
      async_accept(implementation_type& impl,
                   boost::asio::basic_socket<Protocol1, SocketService>& peer,
                   endpoint_type* ,
                   BOOST_ASIO_MOVE_ARG(AcceptHandler) handler,
                   typename std::enable_if<::std::is_convertible<
                       protocol_type, Protocol1>::value>::type* = 0) {
    boost::asio::detail::async_result_init<AcceptHandler,
                                           void(boost::system::error_code)>
        init(BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler));

    if (!is_open(impl)) {
      this->get_io_service().post(
          boost::asio::detail::binder1<decltype(init.handler),
                                       boost::system::error_code>(
              init.handler, boost::system::error_code(
                                udt::common::error::broken_pipe,
                                udt::common::error::get_error_category())));
      return init.result.get();
    }

    if (!impl.p_multiplexer) {
      this->get_io_service().post(
          boost::asio::detail::binder1<decltype(init.handler),
                                       boost::system::error_code>(
              init.handler, boost::system::error_code(
                                udt::common::error::bad_address,
                                udt::common::error::get_error_category())));
      return init.result.get();
    }

    using op =
        io::pending_accept_operation<decltype(init.handler), protocol_type>;
    typename op::ptr p = {
        boost::asio::detail::addressof(init.handler),
        boost_asio_handler_alloc_helpers::allocate(sizeof(op), init.handler),
        0};

    p.p = new (p.v) op(peer, nullptr, init.handler);

    impl.p_acceptor->PushAcceptOp(p.p);

    p.v = p.p = 0;

    return init.result.get();
  }

 private:
  void shutdown_service() {}
};
}  // connected_protocol

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_SOCKET_ACCEPTOR_SERVICE_H_
