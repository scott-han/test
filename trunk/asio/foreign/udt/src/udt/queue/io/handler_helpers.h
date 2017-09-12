#ifndef UDT_QUEUE_IO_HANDLER_HELPERS_H_
#define UDT_QUEUE_IO_HANDLER_HELPERS_H_

#include <boost/asio/detail/bind_handler.hpp>

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace queue {
namespace io {

template <class Poster, class Handler, class... Args>
void PostHandler(Poster& poster, Handler&& handler, Args&&... args) {
  poster.post(boost::asio::detail::bind_handler(std::forward<Handler>(handler),
                                                std::forward<Args>(args)...));
}

}  // io
}  // queue


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_QUEUE_IO_HANDLER_HELPERS_H_
