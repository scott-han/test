#include <type_traits>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <iostream>
#include <mutex>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp> // TODO -- when newer gcc comes out try with std::shared_ptr et.al.
#include <boost/circular_buffer.hpp>

// Deprecated for the time being (unused/unsupported/experimental for a long time).
// #include "foreign/udt/src/udt/ip/udt.h"

#include "async_handler_wrappers.h"

#include "../misc/Circular_Buffer.h"
#include "../misc/Socket.h"

#ifndef DATA_PROCESSORS_SYNAPSE_ASIO_SERVER_H
#define DATA_PROCESSORS_SYNAPSE_ASIO_SERVER_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio {

namespace basio = ::boost::asio;
using basio_tcp = ::boost::asio::ip::tcp;

::std::atomic_uint_fast64_t static Written_Size{0};
::std::atomic_uint_fast64_t static Created_Connections_Size{0};
::std::atomic_uint_fast64_t static Destroyed_Connections_Size{0};

struct tcp_socket : basio_tcp::socket {
	unsigned static constexpr handler_size{
		#ifdef _MSC_VER
			500 // TODO -- was 256, now up due to VS2015 debug usage... TODO see if release mode is different and perhaps can lower it (for all compilers?)
		#else
			256
		#endif
	}; 
	typedef basio_tcp::socket base_type;
	typedef basio_tcp protocol_type;
	tcp_socket(basio::io_service & IO_Service)
	: base_type(IO_Service) {
	}

	void static
	open_acceptor(protocol_type::acceptor & a)
	{
		a.open(basio_tcp::v4());
		#ifdef __WIN32__
			a.set_option(basio::detail::socket_option::boolean<BOOST_ASIO_OS_DEF(SOL_SOCKET), SO_EXCLUSIVEADDRUSE>(true));
		#else
			a.set_option(basio::socket_base::reuse_address(true));
		#endif
	}

	void
	open()
	{
		base_type::open(basio_tcp::v4());
	}

	void Enable_tcp_loopback_fast_path() {
		synapse::misc::Enable_Tcp_Loopback_Fast_Path(base_type::native());
	}

	static ::boost::asio::ip::tcp
	get_query_layer_protocol() 
	{
		return ::boost::asio::ip::tcp::v4();
	}

	base_type &
	get_async_acceptable()
	{
		return *this;
	}
	template <typename Obj>
	void 
	on_accepted(::boost::shared_ptr<Obj> const & o)
	{
		o->on_connect();
	}
	template <typename Obj>
	void
	async_connect(::boost::shared_ptr<Obj> const & o, ::std::string const & host, ::std::string const & port)
	{
		typename protocol_type::resolver endpoints(base_type::get_io_service());
		typename protocol_type::resolver::iterator endpoints_i(endpoints.resolve(typename protocol_type::resolver::query(protocol_type::v4(), host, port)));
		if (endpoints_i == typename protocol_type::resolver::iterator())
			throw ::std::runtime_error(::std::string("could not connect to host(=") + host + "), port(=" + port + ")");
		base_type::async_connect(*endpoints_i, o->get_strand().wrap([o](::boost::system::error_code const &e){
			try {
				if (!e) { 
					if (!o->get_async_error())
						o->on_connect();
				} else
					throw ::std::runtime_error(e.message());
			} catch (::std::exception const & e) { o->put_into_error_state(e); } 
		}));
	}

	::std::string 
	remote_address() const 
	{
		auto const & Endpoint(base_type::remote_endpoint());
		return Endpoint.address().to_string() + ':' + ::std::to_string(Endpoint.port());
	}
};

// Deprecated for the time being (unused/unsupported/experimental for a long time).
#if 0
template <template <class> class CongestionControlAlg = asio::foreign::udt::connected_protocol::congestion::CongestionControl>
struct udt_socket : asio::foreign::udt::ip::udt<CongestionControlAlg>::socket {
	unsigned static constexpr handler_size{256};
	typedef typename asio::foreign::udt::ip::udt<CongestionControlAlg>::socket base_type;
	typedef asio::foreign::udt::ip::udt<CongestionControlAlg> protocol_type;
	udt_socket(basio::io_service & IO_Service)
	: base_type(IO_Service) {
	}

	void
	open()
	{
	}

	void static
	open_acceptor(typename protocol_type::acceptor & a)
	{
		a.open();
	}

	static ::boost::asio::ip::udp
	get_query_layer_protocol() 
	{
		return ::boost::asio::ip::udp::v4();
	}

	base_type &
	get_async_acceptable()
	{
		return *this;
	}
	template <typename Obj>
	void 
	on_accepted(::boost::shared_ptr<Obj> const & o)
	{
		o->on_connect();
	}
	template <typename Obj>
	void
	async_connect(::boost::shared_ptr<Obj> const & o, ::std::string const & host, ::std::string const & port)
	{
		typename protocol_type::resolver endpoints(base_type::get_io_service());
		typename protocol_type::resolver::iterator endpoints_i(endpoints.resolve(typename protocol_type::resolver::query(::boost::asio::ip::udp::v4(), host, port)));
		if (endpoints_i != typename protocol_type::resolver::iterator())
			base_type::async_connect(*endpoints_i, o->get_strand().wrap([o](::boost::system::error_code const &e){
				try {
					if (!e) { 
						if (!o->get_async_error())
							o->on_connect();
					} else
						throw ::std::runtime_error(e.message());
				} catch (::std::exception const & e) { o->put_into_error_state(e); } 
			}));
		else
			throw ::std::runtime_error(::std::string("could not connect to host(=") + host + "), port(=" + port + ")");
	}

	::std::string 
	remote_address() const 
	{
		auto && re(base_type::remote_endpoint());
		auto const & Next_layer_endpoint(re.next_layer_endpoint());
		return Next_layer_endpoint.address().to_string() + ':' + ::std::to_string(Next_layer_endpoint.port()) + ", socket_session " + ::std::to_string(re.socket_id());
	}
};
#endif

template <typename SubStream>
struct ssl_socket : basio::ssl::stream<SubStream> {
	unsigned static constexpr handler_size{352};
	typedef basio::ssl::stream<SubStream> base_type;
	typedef basio_tcp protocol_type;
	ssl_socket(basio::io_service & IO_Service) 
	: base_type(IO_Service, io_service::get_ssl_context()) {
	}
	void 
	close()
	{
		base_type::next_layer().close();
	}

	void
	open()
	{
		base_type::next_layer().open(basio_tcp::v4());
	}

	void static
	open_acceptor(protocol_type::acceptor & a)
	{
		a.open(basio_tcp::v4());
		#ifdef __WIN32__
			a.set_option(basio::detail::socket_option::boolean<BOOST_ASIO_OS_DEF(SOL_SOCKET), SO_EXCLUSIVEADDRUSE>(true));
		#else
			a.set_option(basio::socket_base::reuse_address(true));
		#endif
	}

	void Enable_tcp_loopback_fast_path() {
		synapse::misc::Enable_Tcp_Loopback_Fast_Path(base_type::next_layer().native());
	}

	static ::boost::asio::ip::tcp
	get_query_layer_protocol() 
	{
		return ::boost::asio::ip::tcp::v4();
	}

	SubStream &
	get_async_acceptable()
	{
		return base_type::next_layer();
	}
	template <typename Obj>
	void 
	on_accepted(::boost::shared_ptr<Obj> const & o)
	{
		base_type::async_handshake(::boost::asio::ssl::stream_base::server, o->get_strand().wrap(
			[o](::boost::system::error_code const &e) {
				try {
					if (!e) { 
						if (!o->get_async_error())
							o->on_connect();
					} else
						throw ::std::runtime_error(e.message());
				} catch (::std::exception const & e) { o->put_into_error_state(e); } 
			})
		);
	}

	template <typename Obj>
	void
	async_connect(::boost::shared_ptr<Obj> const & o, ::std::string const & host, ::std::string const & port)
	{
		typedef typename base_type::next_layer_type::protocol_type protocol_type;
		typename protocol_type::resolver endpoints(base_type::get_io_service());
		typename protocol_type::resolver::iterator endpoints_i(endpoints.resolve(typename protocol_type::resolver::query(protocol_type::v4(), host, port)));
		if (endpoints_i == typename protocol_type::resolver::iterator())
			throw ::std::runtime_error(::std::string("could not connect to host(=") + host + "), port(=" + port + ")");
		base_type::next_layer().async_connect(*endpoints_i, o->get_strand().wrap([o, this](::boost::system::error_code const &e){
			try {
				if (!e) { 
					if (!o->get_async_error())
						base_type::async_handshake(::boost::asio::ssl::stream_base::client, o->get_strand().wrap([o](::boost::system::error_code const &e){
							try {
								if (!e) { 
									if (!o->get_async_error())
										o->on_connect();
								} else
									throw ::std::runtime_error(e.message());
							} catch (::std::exception const & e) { o->put_into_error_state(e); } 
						}));
				} else
					throw ::std::runtime_error(e.message());
			} catch (::std::exception const & e) { o->put_into_error_state(e); } 
		}));
	}

	template <typename Option> 
	void 
	set_option(Option && Source) 
	{ 
		base_type::next_layer().set_option(::std::forward<Option>(Source));
	}

	template <typename Option> 
	void 
	get_option(Option & Result) 
	{ 
		base_type::next_layer().get_option(Result);
	}

	::std::string 
	remote_address() const 
	{
		auto const & Endpoint(base_type::next_layer().remote_endpoint());
		return Endpoint.address().to_string() + ':' + ::std::to_string(Endpoint.port());
	}

	auto native_handle() {
		return base_type::next_layer().native_handle();
	}

	bool is_open() const {
		return base_type::next_layer().is_open();
	}
};
typedef ssl_socket<basio_tcp::socket> ssl_tcp_socket;

class Any_Connection_Type {
	::std::string Remote_Address; // keeping separate for future filtering-firewall-policy purposes
	::std::string User_Name; // keeping separate for future user-rights policy purposes
	::std::string User_Password;
	::std::string Description;
	public:
	void Set_Remote_Address(::std::string const & Value) {
		Description = Remote_Address = Value;
	}
	void Set_User_Name_And_Password(::std::string const & Name, ::std::string const & Password) {
		User_Name = Name;
		User_Password = Password;
		Description = User_Name + '@' + Remote_Address;
	}
	auto Get_Description() const {
		return Description;
	}
	virtual void Async_Destroy() = 0;

	enum class Publisher_Sticky_Mode_Bitmask : uint8_t { Replace_Another = 0x1, Replace_Self = 0x2 };
	virtual ::std::underlying_type<Publisher_Sticky_Mode_Bitmask>::type Get_Publisher_Sticky_Mode_Atomic() = 0;
};

// TODO -- later on migrate MaxBufferSize to a runtime arg and initially use lesser amount for buffering (expanding only when the deriving class wants it.. e.g. from amqp max-frame-size negotiation/tuning
template <typename SocketType, unsigned MaxBufferSize, unsigned Offset, typename Deriving, typename WriteCallee>
class connection_1 : public ::boost::enable_shared_from_this<Deriving>, private ::boost::noncopyable, public Any_Connection_Type {

public:
	typedef void (Deriving::*frame_callback_type) (); 
	typedef void (WriteCallee::*write_frame_callback_type) (); 
private:
	typedef void (connection_1::*on_post_callback_type) (); 

	typedef void (connection_1::*on_io_callback_type) (::boost::system::error_code const &, ::std::size_t); 
	typedef void (connection_1::*on_write_io_callback_type) (WriteCallee *, ::boost::system::error_code const &, ::std::size_t); 
	typedef void (connection_1::*On_Deferred_Write_Callback) (WriteCallee *); 

	typedef void * (connection_1::*alloc_callback_type) (unsigned); 
	typedef void (connection_1::*free_callback_type) (void *); 


public:
	typedef SocketType socket_type;
	basic_alloc_type<socket_type::handler_size> allocator;
	basic_alloc_type<socket_type::handler_size> write_allocator;
	basic_alloc_type<socket_type::handler_size> post_write_allocator;
	basic_alloc_type<socket_type::handler_size> Deferred_Write_Handler_Allocator;
	basic_alloc_type<socket_type::handler_size> On_Written_Buffer_Handler_Allocator;
private:

	void * handler_alloc(unsigned size) { return allocator.alloc(size); }
	void handler_free(void * ptr) { return allocator.dealloc(ptr); }
	void * write_handler_alloc(unsigned size) { return write_allocator.alloc(size); }
	void write_handler_free(void * ptr) { return write_allocator.dealloc(ptr); }
	void * post_write_handler_alloc(unsigned size) { return post_write_allocator.alloc(size); }
	void post_write_handler_free(void * ptr) { return post_write_allocator.dealloc(ptr); }
	void * Deferred_Write_Handler_Allocate(unsigned size) { return Deferred_Write_Handler_Allocator.alloc(size); }
	void Deferred_Write_Handler_Deallocate(void * ptr) { return Deferred_Write_Handler_Allocator.dealloc(ptr); }
	void * On_Written_Buffer_Handler_Allocate(unsigned size) { return On_Written_Buffer_Handler_Allocator.alloc(size); }
	void On_Written_Buffer_Handler_Deallocate(void * ptr) { return On_Written_Buffer_Handler_Allocator.dealloc(ptr); }

	template<on_io_callback_type Callback>
	using read_bind_type = handler_type_1<Deriving, on_io_callback_type, alloc_callback_type, free_callback_type, Callback, &connection_1::handler_alloc, &connection_1::handler_free>;

	template<on_write_io_callback_type Callback>
	using write_bind_type = handler_type_2<Deriving, WriteCallee, on_write_io_callback_type, alloc_callback_type, free_callback_type, Callback, &connection_1::write_handler_alloc, &connection_1::write_handler_free>;
	
	template<on_post_callback_type Callback>
	using post_write_bind_type = handler_type_1<Deriving, on_post_callback_type, alloc_callback_type, free_callback_type, Callback, &connection_1::post_write_handler_alloc, &connection_1::post_write_handler_free>;

	template<On_Deferred_Write_Callback Callback>
	using Deferred_Write_Handler = handler_type_2<Deriving, WriteCallee, On_Deferred_Write_Callback, alloc_callback_type, free_callback_type, Callback, &connection_1::Deferred_Write_Handler_Allocate, &connection_1::Deferred_Write_Handler_Deallocate>;

	void On_Written_Buffer(::boost::system::error_code const & e, size_t const Bytes_Size) {
		assert(strand.running_in_this_thread());
		try {
			if (!e) { 
				assert(write_is_in_progress == true);
				write_is_in_progress = false;
				if (!async_error) {

					auto Spaces(write_data.Used());
					assert(Spaces.Total_Size >= Bytes_Size);
					write_data.Consume(Bytes_Size);
					if (Spaces.Total_Size > Bytes_Size)
						Flush_Accumulated_Internal_Buffer(write_data.Used());
					
					if (pending_writechain.empty() == false)
						pop_pending_writechain();
				}
			} else
				throw ::std::runtime_error("On_Written_Buffer " + e.message());
		} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
	}

	using On_Written_Buffer_Handler = handler_type_1<Deriving, on_io_callback_type, alloc_callback_type, free_callback_type, &connection_1::On_Written_Buffer, &connection_1::On_Written_Buffer_Handler_Allocate, &connection_1::On_Written_Buffer_Handler_Deallocate>;

protected:
	using ::boost::enable_shared_from_this<Deriving>::shared_from_this;

	::boost::asio::strand strand;

private:
	socket_type socket;
protected:
	// TODO -- use VirtualAlloc (et. al.) to use HUGE_PAGEs memory instead (trivial task to accomplish later on)
	typename ::std::aligned_storage<MaxBufferSize + Offset>::type data_raw;
	unsigned bytes;
private:

	bool read_is_in_progress{false};
public:
	basio::strand &
	get_strand()
	{
		return strand;
	}
	bool
	is_read_in_progress() const
	{
		return read_is_in_progress;
	}
	bool
	is_write_in_progress_or_imminent() const
	{
		return write_is_in_progress || !pending_writechain.empty();
	}
private:
	bool write_is_in_progress{false};
	bool Deferred_Write_Is_In_Progress{false};
	unsigned target_pending_writechain_capacity;
protected:
	typedef uint_fast32_t Pending_Writechain_Indicator;
	void static Decompose_Pending_Writechain_Indicator(Pending_Writechain_Indicator const & Indicator, bool & Need_Internal_Buffer_Continuous_Space, Pending_Writechain_Indicator & Max_Bytes_Size) {
		Need_Internal_Buffer_Continuous_Space = Indicator & static_cast<Pending_Writechain_Indicator>(1) << sizeof(Pending_Writechain_Indicator) * 8 - 1;
		Max_Bytes_Size = Indicator & static_cast<Pending_Writechain_Indicator>(-1) >> 1;
	}
	Pending_Writechain_Indicator static Compose_Pending_Writechain_Indicator(bool const Need_Internal_Buffer_Continuous_Space, Pending_Writechain_Indicator const & Max_Bytes_Size) {
		assert(Max_Bytes_Size < static_cast<Pending_Writechain_Indicator>(1) << sizeof(Pending_Writechain_Indicator) * 8 - 1);
		return  static_cast<Pending_Writechain_Indicator>(Need_Internal_Buffer_Continuous_Space) << sizeof(Pending_Writechain_Indicator) * 8 - 1 | Max_Bytes_Size;
	}
	::boost::circular_buffer<::std::pair<Pending_Writechain_Indicator, ::std::function<void()>>> pending_writechain;
	char unsigned * data;
#ifndef NDEBUG
	unsigned Data_size{MaxBufferSize};
#endif
	synapse::misc::Circular_Buffer<char unsigned, uint_fast32_t, MaxBufferSize, Offset> write_data;
	bool async_error = false;
private:

	template <write_frame_callback_type Callback>
	void
	on_write(WriteCallee * Callee, ::boost::system::error_code const & e, size_t const) 
	{
		assert(strand.running_in_this_thread());
		try {
			if (!e) { 
				assert(write_is_in_progress == true);
				write_is_in_progress = false;
				if (!async_error) {

					(Callee->*Callback)();

					if (!write_is_in_progress) {
						auto const Spaces(write_data.Used());
						if (Spaces.A_Size)
							Flush_Accumulated_Internal_Buffer(Spaces);
					}
					
					if (pending_writechain.empty() == false)
						pop_pending_writechain();
				}
			} else
				throw ::std::runtime_error("on_write " + e.message());
		} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
	}

	template <write_frame_callback_type Callback>
	void On_Deferred_Write(WriteCallee * Callee) {
		assert(strand.running_in_this_thread());
		assert(Deferred_Write_Is_In_Progress);
		Deferred_Write_Is_In_Progress = false;
		if (!async_error) 
			try {
					(Callee->*Callback)();
					if (pending_writechain.empty() == false)
						pop_pending_writechain();
			} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
	}

	bool Try_Allow_Next_Processing_Chunk(bool Need_Internal_Buffer_Continuous_Space, Pending_Writechain_Indicator Max_Bytes_Size) const {
		auto const Spaces(write_data.Free());
		return !Need_Internal_Buffer_Continuous_Space && (Max_Bytes_Size <= Spaces.Total_Size && !Deferred_Write_Is_In_Progress || !write_is_in_progress) || Need_Internal_Buffer_Continuous_Space && Max_Bytes_Size <= Spaces.A_Size && !Deferred_Write_Is_In_Progress;
	}

	void pop_pending_writechain() {
		// Note, not necessary to test for async_error here because put_into_error_state() clears the pending_writechain and below code tests for it not being empty.
		try {
			assert(strand.running_in_this_thread());
			assert(pending_writechain.empty() == false);

			auto const & Pending_Write(pending_writechain.front());

			bool Need_Internal_Buffer_Continuous_Space;
			Pending_Writechain_Indicator Max_Bytes_Size;
			Decompose_Pending_Writechain_Indicator(Pending_Write.first, Need_Internal_Buffer_Continuous_Space, Max_Bytes_Size);

			if (Try_Allow_Next_Processing_Chunk(Need_Internal_Buffer_Continuous_Space, Max_Bytes_Size)) {
				Pending_Write.second();
				pending_writechain.pop_front();
				assert(pending_writechain.size() < target_pending_writechain_capacity);
				if (write_is_in_progress == false && pending_writechain.empty() == false) {
					if (pending_writechain.capacity() > target_pending_writechain_capacity) 
						pending_writechain.set_capacity(target_pending_writechain_capacity);
					strand.post(post_write_bind_type<&connection_1::pop_pending_writechain>(shared_from_this()));
				}
				// Static polymorphism, should be capable of being inlined into nothing by Deriving types which do not do anything in their implementation.
				static_cast<Deriving*>(this)->On_pop_pending_writechain();
			}
		} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
	}

	template <frame_callback_type Callback>
	void
	on_read(::boost::system::error_code const & e, ::std::size_t bytes) 
	{
		assert(strand.running_in_this_thread());
		try {
			if (!e) {
				assert(read_is_in_progress == true);
				read_is_in_progress = false;
				if (!async_error) {
					assert(bytes > 0);
					this->bytes = bytes;
					(static_cast<Deriving*>(this)->*Callback)();
				}
			} else
				throw ::std::runtime_error("on_read " + e.message());
		} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
	}

private:
	struct Io_completion_condition {

		unsigned Io_size;

		Io_completion_condition(unsigned const Io_size)
		: Io_size(Io_size) {
		}

		::std::size_t
		operator()(::boost::system::error_code const &Last_io_error, ::std::size_t Transfered_so_far)
		{
			assert(Transfered_so_far <= Io_size);
			return Last_io_error ? 0 : Io_size - Transfered_so_far;
		}
	};

	struct Write_IO_Completion_Condition : Io_completion_condition {
		Write_IO_Completion_Condition(unsigned const Io_size) 
		: Io_completion_condition(Io_size) {
			asio::Written_Size.fetch_add(Io_size, ::std::memory_order_relaxed);
		}
	};
protected:
	template <frame_callback_type Callback>
	void
	read(unsigned bytes, unsigned offset = 0)
	{
		assert(strand.running_in_this_thread());
		assert(read_is_in_progress == false);
		assert(Data_size >= offset + bytes);
		read_is_in_progress = true;
		basio::async_read(socket, basio::buffer(data + offset, bytes), Io_completion_condition(bytes), strand.wrap(read_bind_type<&connection_1::on_read<Callback> >(shared_from_this())));
	}
	template <frame_callback_type Callback>
	void
	read_some(unsigned bytes, unsigned offset = 0)
	{
		assert(strand.running_in_this_thread());
		assert(read_is_in_progress == false);
		assert(Data_size >= offset + bytes);
		read_is_in_progress = true;
		socket.async_read_some(basio::buffer(data + offset, bytes), strand.wrap(read_bind_type<&connection_1::on_read<Callback> >(shared_from_this())));
	}

	void Flush_Accumulated_Internal_Buffer( 
		#ifdef _MSC_VER
			typename synapse::misc::Circular_Buffer<char unsigned, uint_fast32_t, MaxBufferSize, Offset>::Space_Information const & Spaces
		#else
			typename decltype(write_data)::Space_Information const & Spaces
		#endif
	) {
		assert(!write_is_in_progress);
		assert(Spaces.A_Size);
		write_is_in_progress = true;
		if (Spaces.B_Size) {
			::std::array<basio::const_buffer, 2> Buffers{{{Spaces.A_Begin, Spaces.A_Size}, {Spaces.B_Begin, Spaces.B_Size}}};
			basio::async_write(socket, Buffers, Write_IO_Completion_Condition(Spaces.Total_Size), strand.wrap(On_Written_Buffer_Handler(shared_from_this())));
		} else {
			basio::async_write(socket, basio::buffer(Spaces.A_Begin, Spaces.A_Size), Write_IO_Completion_Condition(Spaces.A_Size), strand.wrap(On_Written_Buffer_Handler(shared_from_this())));
		}
	}

	template <write_frame_callback_type Callback>
	void Write_Internal_Buffer(WriteCallee * Callee, unsigned const & Bytes) {
		assert(Bytes);
		assert(strand.running_in_this_thread());
		assert(!Deferred_Write_Is_In_Progress);
		write_data.Append(Bytes);
		Deferred_Write_Is_In_Progress = true;
		strand.post(Deferred_Write_Handler<&connection_1::On_Deferred_Write<Callback>>(shared_from_this(), Callee));
		if (!write_is_in_progress) // Todo write specialised version of Flush, because should be just the start of the circular buffer (so already know whether to call scatter write or just one)
			Flush_Accumulated_Internal_Buffer(write_data.Used());
	}

	template <write_frame_callback_type Callback>
	void Write_External_Buffer(WriteCallee * callee, char unsigned const * const bfr, unsigned const bytes) {
		assert(strand.running_in_this_thread());
		auto const Spaces(write_data.Free());
		if (Spaces.Total_Size >= bytes) {
			if (Spaces.A_Size >= bytes)
				::memcpy(Spaces.A_Begin, bfr, bytes);
			else { 
				::memcpy(Spaces.A_Begin, bfr, Spaces.A_Size);
				assert(Spaces.B_Size >= bytes - Spaces.A_Size);
				::memcpy(Spaces.B_Begin, bfr + Spaces.A_Size, bytes - Spaces.A_Size);
			}
			Write_Internal_Buffer<Callback>(callee, bytes);
		} else {
			assert(write_is_in_progress == false);
			write_is_in_progress = true;
			basio::async_write(socket, basio::buffer(bfr, bytes), Write_IO_Completion_Condition(bytes), strand.wrap(write_bind_type<&connection_1::on_write<Callback>>(shared_from_this(), callee)));
		}
	}

	template <write_frame_callback_type Callback, typename AsioConstBufferSequence>
	void Write_External_Buffers(WriteCallee * callee, AsioConstBufferSequence const & buffers, unsigned Cumulative_buffers_size) {
		assert(strand.running_in_this_thread());
		assert(Cumulative_buffers_size);
		#ifndef NDEBUG
			unsigned tmp(0);
			for (auto && i(buffers.begin()); i != buffers.end(); ++i)
				tmp += basio::buffer_size(*i);
			assert(tmp == Cumulative_buffers_size);
		#endif

		auto Spaces(write_data.Free());
		if (Spaces.Total_Size >= Cumulative_buffers_size) {
			#ifndef NDEBUG
				auto Remaining_Bytes_To_Write(Cumulative_buffers_size);
			#endif
			auto Temporary_Data_Pointer(Spaces.A_Begin);

			auto * Free_Size(&Spaces.A_Size);
			assert(*Free_Size);
			for (auto && i(buffers.begin()); i != buffers.end(); ++i) {
				auto Buffer_Size(basio::buffer_size(*i));
				auto Buffer_Data(basio::buffer_cast<char unsigned const *>(*i));
				do {
					if (!*Free_Size) {
						assert(Spaces.B_Size);
						assert(&Spaces.A_Size == Free_Size);
						Free_Size = &Spaces.B_Size;
						Temporary_Data_Pointer = Spaces.B_Begin;
					}
					auto const Chunk_Size(::std::min<uint_fast32_t>(*Free_Size, Buffer_Size));

					::memcpy(Temporary_Data_Pointer, Buffer_Data, Chunk_Size);
					
					Temporary_Data_Pointer += Chunk_Size;
					Buffer_Data += Chunk_Size;
					#ifndef NDEBUG
						Remaining_Bytes_To_Write -= Chunk_Size;
					#endif
					*Free_Size -= Chunk_Size;
					Buffer_Size -= Chunk_Size;
				} while (Buffer_Size);
			}
			assert(!Remaining_Bytes_To_Write);
			Write_Internal_Buffer<Callback>(callee, Cumulative_buffers_size);

		} else {
			assert(write_is_in_progress == false);
			write_is_in_progress = true;
			basio::async_write(socket, buffers, Write_IO_Completion_Condition(Cumulative_buffers_size), strand.wrap(write_bind_type<&connection_1::on_write<Callback>>(shared_from_this(), callee)));
		}
	}

	template <typename Auto>
	void inline
	dispatch(Auto && expr, bool const Need_Internal_Buffer_Continuous_Space = false, Pending_Writechain_Indicator const & Max_Bytes_Size = ::std::numeric_limits<Pending_Writechain_Indicator>::max() >> 1) 
	{
		assert(strand.running_in_this_thread());
		if (pending_writechain.empty() == true && Try_Allow_Next_Processing_Chunk(Need_Internal_Buffer_Continuous_Space, Max_Bytes_Size))
			expr();
		else {
			assert(pending_writechain.size() < target_pending_writechain_capacity);
			if (pending_writechain.size() == pending_writechain.capacity())
				pending_writechain.set_capacity(target_pending_writechain_capacity);
			// TODO -- when/if Boost supports arg forwarding (emplace) for circular buffer, use piecewise_construct!
			pending_writechain.push_back(::std::make_pair(Compose_Pending_Writechain_Indicator(Need_Internal_Buffer_Continuous_Space, Max_Bytes_Size), ::std::forward<Auto>(expr)));
			// Static polymorphism, should be capable of being inlined into nothing by Deriving types which do not do anything in their implementation.
			static_cast<Deriving*>(this)->On_push_pending_writechain();
		}
	}

	char unsigned * 
	get_default_data() noexcept
	{
		return reinterpret_cast<char unsigned*>(&data_raw) + Offset;
	}

	void 
	put_into_error_state(::std::exception const & e) 
	{
		assert(strand.running_in_this_thread());
		synapse::misc::log("synapse::asio::connection_1(=" + ::boost::lexical_cast<::std::string>(this) + ") error(=" + e.what() + ") peer(=" + Any_Connection_Type::Get_Description() + ")\n", true);
		async_error = true;

		// TODO -- more elegant perhaps (or at least watch out that no callbacks are registered and left uncalled in such a mechanisms (otherwise memory will not be freed and will leak)...
		pending_writechain.clear();
		socket.close();
	}

	void
	set_pending_writechain_capacity(unsigned x)
	{
		target_pending_writechain_capacity = x + 1;
		assert(pending_writechain.size() <= target_pending_writechain_capacity);
	}

	void Pin_IO_Buffers() {
		static_assert(sizeof(::HANDLE) == sizeof(::SOCKET), "::HANDLE vs ::SOCKET casting");
		if (
			!::SetFileIoOverlappedRange(reinterpret_cast<::HANDLE>(static_cast<::SOCKET>(socket.native_handle())), static_cast<char unsigned *>(allocator.Get_Cached_Data()), socket_type::handler_size)
			|| !::SetFileIoOverlappedRange(reinterpret_cast<::HANDLE>(static_cast<::SOCKET>(socket.native_handle())), static_cast<char unsigned *>(write_allocator.Get_Cached_Data()), socket_type::handler_size)
			|| !::SetFileIoOverlappedRange(reinterpret_cast<::HANDLE>(static_cast<::SOCKET>(socket.native_handle())), static_cast<char unsigned *>(On_Written_Buffer_Handler_Allocator.Get_Cached_Data()), socket_type::handler_size)
		) {
			if (data_processors::synapse::database::nice_performance_degradation < -39)
				throw ::std::runtime_error("SetFileIoOverlappedRange");
			else if (data_processors::synapse::database::nice_performance_degradation < 1)
				synapse::misc::log("WARNING: PERFORMANCE DEGRADATION!!!	could not SetFileIoOverlappedRange on socket.\n", true);
		}
	}

public:
	connection_1(unsigned max_pending_write_resquests)
	: strand(io_service::get()) 
		, socket(strand.get_io_service())
		, target_pending_writechain_capacity(max_pending_write_resquests + 1) // +1 is for one in the queue before being popped
		, pending_writechain(target_pending_writechain_capacity)
		, data(reinterpret_cast<char unsigned*>(&data_raw) + Offset) {
		asio::Created_Connections_Size.fetch_add(1u, ::std::memory_order_relaxed);
	}

	~connection_1()
	{
		if (socket.is_open())
			socket.close();
		asio::Destroyed_Connections_Size.fetch_add(1u, ::std::memory_order_relaxed);
	}

	bool
	get_async_error() const
	{
		return async_error;
	}

	socket_type & 
	get_socket() 
	{
		return socket;
	}

	void
	async_on_accepted(::boost::shared_ptr<Deriving> const & this_)
	{
		strand.dispatch([this_, this]() {
			try {
				Any_Connection_Type::Set_Remote_Address(socket.remote_address());
				static_cast<Deriving*>(this)->On_accept();
				socket.on_accepted(this_);
			} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
		});
	}

	void
	async_connect(::boost::shared_ptr<Deriving> const & this_, ::std::string const & host, ::std::string const & port) 
	{
		strand.dispatch([this_, this, host, port]() {
			try {
				Any_Connection_Type::Set_Remote_Address(host + ':' + port);
				socket.async_connect(this_, host, port);
			} catch (::std::exception const & e) { static_cast<Deriving*>(this)->put_into_error_state(e); } 
		});
	}

	void
	on_connect()
	{
		assert(strand.running_in_this_thread());
		Pin_IO_Buffers();
		// setup quitting process
		::boost::weak_ptr<Deriving> weak_scope(shared_from_this()); 
		synapse::asio::quitter::get().add_quit_handler([this, weak_scope](synapse::asio::quitter::Scoped_Membership_Registration_Type && Scoped_Membership_Registration){
			if (auto scope = weak_scope.lock())
				strand.post([this, scope, On_Quit_Scoped_Membership_Registration(::std::move(Scoped_Membership_Registration))]() mutable {
					this->On_Quit_Scoped_Membership_Registration = ::std::move(On_Quit_Scoped_Membership_Registration);
					assert(!On_Quit_Scoped_Membership_Registration);
					if (!this->On_Quit_Scoped_Membership_Registration && !async_error)
						static_cast<Deriving*>(this)->put_into_error_state(::std::runtime_error("quitting initiated"));
				});
		});
	}
	synapse::asio::quitter::Scoped_Membership_Registration_Type On_Quit_Scoped_Membership_Registration;
};

template <typename Connection>
class server_1 : public ::boost::enable_shared_from_this<server_1<Connection>> {
	basio::strand strand{io_service::get()};
	typedef ::boost::enable_shared_from_this<server_1<Connection>> base_type;
	typedef typename Connection::socket_type protocol_traits;
	typename protocol_traits::protocol_type::acceptor acceptor;
	void
	on_accept(::boost::shared_ptr<Connection> & new_connection, ::boost::system::error_code const & e) 
	{
		assert(strand.running_in_this_thread());
		if (acceptor.is_open()) {
			if (!e)
				new_connection->async_on_accepted(new_connection); 
			else 
				synapse::misc::log("Socket accceptor on_accept error(=" + e.message() + ")\n", true);
			async_accept();
		}
	}
	uint_fast32_t const Socket_receive_buffer_size;
	uint_fast32_t const Socket_send_buffer_size;
public:
	server_1(::std::string const & host, ::std::string const & port, unsigned const listen_size, uint_fast32_t const Socket_receive_buffer_size = -1, uint_fast32_t const Socket_send_buffer_size = -1, bool const Tcp_loopback_fast_path = false)
	: acceptor(strand.get_io_service()), Socket_receive_buffer_size(Socket_receive_buffer_size), Socket_send_buffer_size(Socket_send_buffer_size)  {
		typename protocol_traits::protocol_type::resolver endpoints(strand.get_io_service());
		typename protocol_traits::protocol_type::resolver::iterator endpoints_i(endpoints.resolve(typename protocol_traits::protocol_type::resolver::query(protocol_traits::get_query_layer_protocol(), host, port)));
		while (endpoints_i != typename protocol_traits::protocol_type::resolver::iterator()) {
			protocol_traits::open_acceptor(acceptor);

			if (Tcp_loopback_fast_path)
				synapse::misc::Enable_Tcp_Loopback_Fast_Path(acceptor.native());

			// TODO set socket buffer to BDP (bandwidth delay product).
			// OR -- todo, zero-TCP stack is interesting, but at least on the reading side it ought to have multiple overlapped pending requests (and currently, w.r.t. thread-safety of boost.asio this is yet to be experimented with)
			//int sendbuff = 1500 * 1024;
			//::setsockopt(socket.native(), SOL_SOCKET, SO_RCVBUF, (char*)&sendbuff, sizeof(sendbuff));
			//sendbuff = 0;
			//::setsockopt(socket.native(), SOL_SOCKET, SO_SNDBUF, (char*)&sendbuff, sizeof(sendbuff));
			// also, consider that "nodelay" option may not be of relevance if zero-TCP stack is in use...
			if (Socket_receive_buffer_size != static_cast<decltype(Socket_receive_buffer_size)>(-1))
				acceptor.set_option(basio::socket_base::receive_buffer_size(Socket_receive_buffer_size)); // 8192
			if (Socket_send_buffer_size != static_cast<decltype(Socket_send_buffer_size)>(-1))
				acceptor.set_option(basio::socket_base::send_buffer_size(Socket_send_buffer_size)); // 8192
			//acceptor.set_option(basio::ip::tcp::no_delay(false));

			::boost::system::error_code ec;
			acceptor.bind(*endpoints_i, ec);
			if (!ec) 
				break;
			else {
				acceptor.close();
				++endpoints_i;
			}
		}

		if (endpoints_i != typename protocol_traits::protocol_type::resolver::iterator())
			acceptor.listen(listen_size); // TODO
		else
			throw ::std::runtime_error(::std::string("could not bind to host(=") + host + "), port(=" + port + ") as per host:port in the '--listen_at' option");
	}
	void
	add_quit_handler()
	{
		// setup quitting process
		::boost::weak_ptr<server_1> weak_scope(base_type::shared_from_this()); 
		synapse::asio::quitter::get().add_quit_handler([this, weak_scope](synapse::asio::quitter::Scoped_Membership_Registration_Type && Scoped_Membership_Registration){
			if (auto scope = weak_scope.lock())
				strand.post([this, scope, On_Quit_Scoped_Membership_Registration(::std::move(Scoped_Membership_Registration))]() mutable {
					this->On_Quit_Scoped_Membership_Registration = ::std::move(On_Quit_Scoped_Membership_Registration);
					assert(!On_Quit_Scoped_Membership_Registration);
					if (!this->On_Quit_Scoped_Membership_Registration)
						acceptor.close();
				});
		});
	}
	synapse::asio::quitter::Scoped_Membership_Registration_Type On_Quit_Scoped_Membership_Registration;
	void
	async_accept()
	{
		::boost::shared_ptr<Connection> new_connection(new Connection(Socket_receive_buffer_size, Socket_send_buffer_size));
		// TODO -- try new c++11 lambda syntax...
		acceptor.async_accept(new_connection->get_socket().get_async_acceptable(), strand.wrap(::boost::bind(&server_1::on_accept, base_type::shared_from_this(), new_connection, basio::placeholders::error)));
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
