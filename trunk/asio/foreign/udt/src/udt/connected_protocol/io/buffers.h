#ifndef UDT_CONNECTED_PROTOCOL_IO_BUFFERS_H_
#define UDT_CONNECTED_PROTOCOL_IO_BUFFERS_H_

#include <boost/asio/buffer.hpp>


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio { namespace foreign { namespace udt {
namespace connected_protocol {
namespace io {

template <class BufferType>
class fixed_buffer_sequence {
 public:
  using buffer_type = std::vector<BufferType>;
  using value_type = typename buffer_type::value_type;
  using iterator = typename buffer_type::iterator;
  using const_iterator = typename buffer_type::const_iterator;

  fixed_buffer_sequence() : buffers_() {}

  template <class BufferSequence>
  fixed_buffer_sequence(const BufferSequence& buffers)
      : buffers_() {
    for (const auto& buffer : buffers) {
      buffers_.push_back(buffer);
    }
  }

  iterator begin() { return buffers_.begin(); }
  iterator end() { return buffers_.end(); }

  const_iterator begin() const { return buffers_.begin(); }
  const_iterator end() const { return buffers_.end(); }

  void push_back(const value_type& val) { buffers_.push_back(val); }
  void push_back(value_type&& val) { buffers_.push_back(std::move(val)); }

 private:
  buffer_type buffers_;
};

using fixed_mutable_buffer_sequence =
    fixed_buffer_sequence<boost::asio::mutable_buffer>;

using fixed_const_buffer_sequence =
    fixed_buffer_sequence<boost::asio::const_buffer>;

}  // io
}  // connected_protocol


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif  // UDT_CONNECTED_PROTOCOL_IO_BUFFERS_H_
