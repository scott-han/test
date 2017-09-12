#include <atomic>

#ifndef DATA_PROCESSORS_SYNAPSE_ASIO_ASYNC_HANDLER_WRAPPERS_H
#define DATA_PROCESSORS_SYNAPSE_ASIO_ASYNC_HANDLER_WRAPPERS_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace asio {

// temporary monitoring statistics hack (to help see if we are on the right track)
::std::atomic_bool static bad_buffering;

	template <unsigned Size>
	class basic_alloc_type : private ::boost::noncopyable {
		typename ::std::aligned_storage<Size>::type data;
		bool used = false;
#ifndef NDEBUG
std::atomic_bool blah;
#endif
	public:

#ifndef NDEBUG
basic_alloc_type()
{
	blah = false;
}
#endif
		void* 
		alloc(::std::size_t size)
		{
			if (used == false && size < Size) {
				used = true;
				return &data;
			} else {
				bad_buffering = true;
				return ::operator new(size);
			}
		}

		void 
		dealloc(void* ptr)
		{
#ifndef NDEBUG
			if (blah == true)
				assert(0);
			blah = true;
#endif
			if (ptr == &data)
				used = false;
			else
				::operator delete(ptr);
#ifndef NDEBUG
			blah = false;
#endif
		}

		void * Get_Cached_Data() {
			return &data;
		}
	};
	template <typename SharedPtrType, typename AllocCallbackType, typename FreeCallbackType, AllocCallbackType Alloc, FreeCallbackType Free>
	class base_handler_type
	{
	protected:
		::boost::shared_ptr<SharedPtrType> obj;
	public:
		base_handler_type(::boost::shared_ptr<SharedPtrType> && obj)
		: obj(::std::move(obj))
		{
		}
		base_handler_type(::boost::shared_ptr<SharedPtrType> const & obj)
		: obj(obj)
		{
		}
		base_handler_type(base_handler_type && rhs)
		: obj(::std::move(rhs.obj)) 
		{
		}
		base_handler_type(base_handler_type const &) = default;
		friend void * 
		asio_handler_allocate(std::size_t size, base_handler_type* me)
		{
			return (me->obj.get()->*Alloc)(size);
		}

		friend void 
		asio_handler_deallocate(void* pointer, std::size_t, base_handler_type* me)
		{
			(me->obj.get()->*Free)(pointer);
		}
	};

	template <typename SharedPtrType, typename CallbackType, typename AllocCallbackType, typename FreeCallbackType, CallbackType Callback, AllocCallbackType Alloc, FreeCallbackType Free>
	class handler_type_1 : public base_handler_type<SharedPtrType, AllocCallbackType, FreeCallbackType, Alloc, Free>
	{
		typedef base_handler_type<SharedPtrType, AllocCallbackType, FreeCallbackType, Alloc, Free> base_type;
		using base_type::obj;
	public:
		handler_type_1(::boost::shared_ptr<SharedPtrType> && obj) : base_type(::std::move(obj)) { }
		handler_type_1(::boost::shared_ptr<SharedPtrType> const & obj) : base_type(obj) { }
		handler_type_1(handler_type_1 && rhs) : base_type(::std::move(rhs)) { }
		handler_type_1(handler_type_1 const & rhs) = default;
		template <typename... Args>
		void
		operator()(Args&&... args)
		{
			(obj.get()->*Callback)(::std::forward<Args>(args)...);
		}
	};

	// TODO -- just specialize from handler_type_1
	template <typename SharedPtrType, typename FinalCallee, typename CallbackType, typename AllocCallbackType, typename FreeCallbackType, CallbackType Callback,  AllocCallbackType Alloc, FreeCallbackType Free>
	class handler_type_2 : public base_handler_type<SharedPtrType, AllocCallbackType, FreeCallbackType, Alloc, Free>
	{
		typedef base_handler_type<SharedPtrType, AllocCallbackType, FreeCallbackType, Alloc, Free> base_type;
		using base_type::obj;
		FinalCallee * const final_callee;
	public:
		handler_type_2(::boost::shared_ptr<SharedPtrType> && obj, FinalCallee * final_callee) : base_type(::std::move(obj)), final_callee(final_callee) { }
		handler_type_2(::boost::shared_ptr<SharedPtrType> const & obj, FinalCallee * final_callee) : base_type(obj), final_callee(final_callee) { }
		handler_type_2(handler_type_2 && rhs) : base_type(::std::move(rhs)), final_callee(rhs.final_callee) { }
		handler_type_2(handler_type_2 const & rhs) = default;
		template <typename... Args>
		void
		operator()(Args... args)
		{
			(obj.get()->*Callback)(final_callee, ::std::forward<Args>(args)...);
		}
	};
	
	

// be careful with this one (faster but not for every type of the architectural structure/design)
template <typename ObjType, typename CallbackType, typename AllocCallbackType, typename FreeCallbackType, CallbackType Callback, AllocCallbackType Alloc, FreeCallbackType Free>
class handler_type_5
{
	ObjType & obj;
public:
	handler_type_5(ObjType & obj) : obj(obj) { }
	template <typename... Args>
	void
	operator()(Args&&... args)
	{
		(obj.*Callback)(::std::forward<Args>(args)...);
	}
	friend void * 
	asio_handler_allocate(std::size_t size, handler_type_5 * me)
	{
		return (me->obj.*Alloc)(size);
	}
	friend void 
	asio_handler_deallocate(void* pointer, std::size_t, handler_type_5 * me)
	{
		(me->obj.*Free)(pointer);
	}
};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
