#ifndef UDT_QUEUE_ASYNC_QUEUE_SERVICE_H_
#define UDT_QUEUE_ASYNC_QUEUE_SERVICE_H_

#include <cstdint>

#include <atomic>
#include <memory>
#include <type_traits>

#include <boost/asio/detail/op_queue.hpp>
#include <boost/asio/io_service.hpp>

#include <boost/bind.hpp>
#include <mutex>
#include "../../../../../../misc/sync.h"

#include "../common/error/error.h"

#include "io/get_op.h"
#include "io/push_op.h"
#include "io/handler_helpers.h"


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace queue {

template <class Ttype, class TContainer, uint32_t QueueMaxSize,
          uint32_t OPQueueMaxSize>
class basic_async_queue_service
    : public boost::asio::detail::service_base<basic_async_queue_service<
          Ttype, TContainer, QueueMaxSize, OPQueueMaxSize>> {
 private:
  typedef Ttype T;
  typedef TContainer Container;

 public:
  typedef T value_type;
  typedef Container container_type;
  enum { kQueueMaxSize = QueueMaxSize, kOPQueueMaxSize = OPQueueMaxSize };

  struct implementation_type {
    std::shared_ptr<std::atomic<bool>> p_valid;
    std::shared_ptr<std::atomic<bool>> p_open;

    mutable std::unique_ptr<synapse::misc::recursive_mutex> p_container_mutex;
    Container container;

    mutable std::unique_ptr<synapse::misc::recursive_mutex> p_get_op_queue_mutex;
    std::unique_ptr<boost::asio::detail::op_queue<
        io::basic_pending_get_operation<T>>> p_get_op_queue;
    uint32_t get_op_queue_size;
    std::unique_ptr<boost::asio::io_service::work> p_get_work;

    mutable std::unique_ptr<synapse::misc::recursive_mutex> p_push_op_queue_mutex;
    std::unique_ptr<boost::asio::detail::op_queue<
        io::basic_pending_push_operation<T>>> p_push_op_queue;
    uint32_t push_op_queue_size;
    std::unique_ptr<boost::asio::io_service::work> p_push_work;
  };

 public:
  explicit basic_async_queue_service(boost::asio::io_service& io_service)
      : boost::asio::detail::service_base<basic_async_queue_service>(
            io_service) {}

  virtual ~basic_async_queue_service() {}

  void construct(implementation_type& impl) {
    impl.p_valid = std::make_shared<std::atomic<bool>>(true);
    impl.p_open = std::make_shared<std::atomic<bool>>(true);
    impl.p_container_mutex =
        std::unique_ptr<synapse::misc::recursive_mutex>(new synapse::misc::recursive_mutex());
    impl.p_get_op_queue_mutex =
        std::unique_ptr<synapse::misc::recursive_mutex>(new synapse::misc::recursive_mutex());
    impl.p_push_op_queue_mutex =
        std::unique_ptr<synapse::misc::recursive_mutex>(new synapse::misc::recursive_mutex());
    impl.p_get_op_queue = std::unique_ptr<
        boost::asio::detail::op_queue<io::basic_pending_get_operation<T>>>(
        new boost::asio::detail::op_queue<
            io::basic_pending_get_operation<T>>());
    impl.p_push_op_queue = std::unique_ptr<
        boost::asio::detail::op_queue<io::basic_pending_push_operation<T>>>(
        new boost::asio::detail::op_queue<
            io::basic_pending_push_operation<T>>());
    impl.push_op_queue_size = 0;
    impl.get_op_queue_size = 0;
  }

  void destroy(implementation_type& impl) {
    *impl.p_valid = false;
    *impl.p_open = false;

    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock1(*impl.p_container_mutex);
      while (!impl.container.empty()) {
        impl.container.pop();
      }
    }
    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock2(*impl.p_push_op_queue_mutex);
      while (!impl.p_push_op_queue->empty()) {
        impl.p_push_op_queue->pop();
      }
      impl.p_push_op_queue.reset();
      impl.push_op_queue_size = 0;
      impl.p_push_work.reset();
    }
    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock3(*impl.p_get_op_queue_mutex);
      while (!impl.p_get_op_queue->empty()) {
        impl.p_get_op_queue->pop();
      }
      impl.p_get_op_queue.reset();
      impl.get_op_queue_size = 0;
      impl.p_get_work.reset();
    }

    impl.p_container_mutex.reset();
    impl.p_get_op_queue_mutex.reset();
    impl.p_push_op_queue_mutex.reset();
  }

  void move_construct(implementation_type& impl, implementation_type& other) {
    impl = std::move(other);
  }

  void move_assign(implementation_type& impl,
                   basic_async_queue_service& other_service,
                   implementation_type& other) {
    impl = std::move(other);
  }

  boost::system::error_code push(implementation_type& impl, T element,
                                 boost::system::error_code& ec) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_container_mutex);

    if (!*impl.p_open) {
      ec.assign(udt::common::error::broken_pipe,
                udt::common::error::get_error_category());
      return ec;
    }

    if (impl.container.size() >= QueueMaxSize) {
      ec.assign(udt::common::error::buffer_is_full_error,
                udt::common::error::get_error_category());
      return ec;
    }

    impl.container.push(std::move(element));

    this->get_io_service().post(
        boost::bind(&basic_async_queue_service::HandleGetQueues, this, &impl,
                    impl.p_valid));

    ec.assign(udt::common::error::success, udt::common::error::get_error_category());
    return ec;
  }

  template <class Handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(Handler, void(boost::system::error_code))
      async_push(implementation_type& impl, T element, Handler&& handler) {
    boost::asio::detail::async_result_init<Handler,
                                           void(boost::system::error_code)>
        init(std::forward<Handler>(handler));

    if (!*impl.p_open) {
      io::PostHandler(
          this->get_io_service(), init.handler,
          boost::system::error_code(udt::common::error::broken_pipe,
                                    udt::common::error::get_error_category()));

      return init.result.get();
    }

    if (impl.push_op_queue_size >= OPQueueMaxSize) {
      io::PostHandler(
          this->get_io_service(), init.handler,
          boost::system::error_code(udt::common::error::buffer_is_full_error,
                                    udt::common::error::get_error_category()));

      return init.result.get();
    }

    typedef io::pending_push_operation<
        typename ::boost::asio::handler_type<
            Handler, void(boost::system::error_code)>::type,
        T> op;
    typename op::ptr p = {
        boost::asio::detail::addressof(init.handler),
        boost_asio_handler_alloc_helpers::allocate(sizeof(op), init.handler),
        0};
    p.p = new (p.v) op(init.handler, std::move(element));

    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_push_op_queue_mutex);

      impl.p_push_op_queue->push(p.p);
      ++(impl.push_op_queue_size);
      if (!impl.p_push_work) {
        impl.p_push_work = std::unique_ptr<boost::asio::io_service::work>(
            new boost::asio::io_service::work(this->get_io_service()));
      }
    }

    p.v = p.p = 0;

    this->get_io_service().post(
        boost::bind(&basic_async_queue_service::HandlePushQueues, this, &impl,
                    impl.p_valid));

    return init.result.get();
  }

  T get(implementation_type& impl, boost::system::error_code& ec) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_container_mutex);

    if (!*impl.p_open) {
      ec.assign(udt::common::error::broken_pipe,
                udt::common::error::get_error_category());
      return T();
    }

    if (impl.container.empty()) {
      ec.assign(udt::common::error::io_error,
                udt::common::error::get_error_category());
      return T();
    }

    auto element = std::move(impl.container.front());
    impl.container.pop();

    this->get_io_service().post(
        boost::bind(&basic_async_queue_service::HandlePushQueues, this, &impl,
                    impl.p_valid));

    ec.assign(udt::common::error::success, udt::common::error::get_error_category());
    return element;
  }

  template <class Handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(Handler, void(boost::system::error_code, T))
      async_get(implementation_type& impl, Handler&& handler) {
    boost::asio::detail::async_result_init<Handler,
                                           void(boost::system::error_code, T)>
        init(std::forward<Handler>(handler));

    if (!*impl.p_open) {
      io::PostHandler(
          this->get_io_service(), init.handler,
          boost::system::error_code(udt::common::error::broken_pipe,
                                    udt::common::error::get_error_category()),
          T());

      return init.result.get();
    }

    if ((impl.get_op_queue_size) >= OPQueueMaxSize) {
      io::PostHandler(
          this->get_io_service(), init.handler,
          boost::system::error_code(udt::common::error::buffer_is_full_error,
                                    udt::common::error::get_error_category()),
          T());

      return init.result.get();
    }

    typedef io::pending_get_operation<
        typename ::boost::asio::handler_type<
            Handler, void(boost::system::error_code, T)>::type,
        T> op;
    typename op::ptr p = {
        boost::asio::detail::addressof(init.handler),
        boost_asio_handler_alloc_helpers::allocate(sizeof(op), init.handler),
        0};
    p.p = new (p.v) op(init.handler);

    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_get_op_queue_mutex);

      impl.p_get_op_queue->push(p.p);
      ++(impl.get_op_queue_size);
      if (!impl.p_get_work) {
        impl.p_get_work = std::unique_ptr<boost::asio::io_service::work>(
            new boost::asio::io_service::work(this->get_io_service()));
      }
    }

    p.v = p.p = 0;

    this->get_io_service().post(
        boost::bind(&basic_async_queue_service::HandleGetQueues, this, &impl,
                    impl.p_valid));

    return init.result.get();
  }

  bool empty(const implementation_type& impl) const {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_container_mutex);
    return impl.container.empty();
  }

  std::size_t size(const implementation_type& impl) const {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_container_mutex);
    return impl.container.size();
  }

  void clear(implementation_type& impl) {
    ::std::lock_guard<synapse::misc::recursive_mutex> lock(*impl.p_container_mutex);
    while (!impl.container.empty()) {
      impl.container.pop();
    }

    HandlePushQueues(&impl, impl.p_valid);
  }

  boost::system::error_code close(implementation_type& impl,
                                  boost::system::error_code& ec) {
    *impl.p_open = false;

    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock1(*impl.p_get_op_queue_mutex);
      while (!impl.p_get_op_queue->empty()) {
        auto op = impl.p_get_op_queue->front();
        impl.p_get_op_queue->pop();
        --(impl.get_op_queue_size);

        op->complete(
            boost::system::error_code(udt::common::error::operation_canceled,
                                      udt::common::error::get_error_category()),
            T());
      }

      impl.p_get_work.reset();
    }

    {
      ::std::lock_guard<synapse::misc::recursive_mutex> lock1(*impl.p_push_op_queue_mutex);
      while (!impl.p_push_op_queue->empty()) {
        auto op = impl.p_push_op_queue->front();
        impl.p_push_op_queue->pop();
        --(impl.push_op_queue_size);

        op->complete(
            boost::system::error_code(udt::common::error::operation_canceled,
                                      udt::common::error::get_error_category()));
      }

      impl.p_push_work.reset();
    }

    clear(impl);

    ec.assign(udt::common::error::success, udt::common::error::get_error_category());
    return ec;
  }

 private:
  void HandlePushQueues(implementation_type* p_impl,
                        std::shared_ptr<std::atomic<bool>> p_valid) {
    if (!(*p_valid).load() || !p_impl) {
      return;
    }

    ::std::lock_guard<synapse::misc::recursive_mutex> lock1(*p_impl->p_container_mutex);
    ::std::lock_guard<synapse::misc::recursive_mutex> lock2(*p_impl->p_push_op_queue_mutex);

    if (!*p_impl->p_open) {
      return;
    }

    if ((p_impl->container.size() >= QueueMaxSize) ||
        p_impl->p_push_op_queue->empty()) {
      return;
    }

    auto op = std::move(p_impl->p_push_op_queue->front());
    p_impl->p_push_op_queue->pop();
    --(p_impl->push_op_queue_size);

    auto element = op->element();
    p_impl->container.push(std::move(element));

    auto do_complete =
        [op]() mutable { op->complete(boost::system::error_code()); };
    this->get_io_service().post(do_complete);

    if (p_impl->p_push_op_queue->empty()) {
      p_impl->p_push_work.reset();
    }

    HandleGetQueues(p_impl, p_valid);
  }

  void HandleGetQueues(implementation_type* p_impl,
                       std::shared_ptr<std::atomic<bool>> p_valid) {
    if (!*p_valid || !p_impl->p_container_mutex) {
      return;
    }

    ::std::lock_guard<synapse::misc::recursive_mutex> lock1(*p_impl->p_container_mutex);
    ::std::lock_guard<synapse::misc::recursive_mutex> lock2(*p_impl->p_get_op_queue_mutex);

    if (!*p_impl->p_open) {
      return;
    }

    if (p_impl->container.empty() || p_impl->p_get_op_queue->empty()) {
      HandlePushQueues(p_impl, p_valid);
      return;
    }

    auto element = std::move(p_impl->container.front());
    p_impl->container.pop();

    auto op = std::move(p_impl->p_get_op_queue->front());
    p_impl->p_get_op_queue->pop();
    --(p_impl->get_op_queue_size);

    auto do_complete = [element, op]() mutable {
      op->complete(boost::system::error_code(), std::move(element));
    };
    this->get_io_service().post(do_complete);

    if (p_impl->p_get_op_queue->empty()) {
      p_impl->p_get_work.reset();
    }

    HandlePushQueues(p_impl, p_valid);
  }

  void shutdown_service() {}
};

}  // queue


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_QUEUE_ASYNC_QUEUE_SERVICE_H_
