//
// detail/impl/win_iocp_io_service.ipp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_DETAIL_IMPL_WIN_IOCP_IO_SERVICE_IPP
#define BOOST_ASIO_DETAIL_IMPL_WIN_IOCP_IO_SERVICE_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>

#if defined(BOOST_ASIO_HAS_IOCP)

#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/detail/cstdint.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/asio/detail/limits.hpp>
#include <boost/asio/detail/throw_error.hpp>
#include <boost/asio/detail/win_iocp_io_service.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace detail {

struct win_iocp_io_service::work_finished_on_block_exit
{
  ~work_finished_on_block_exit()
  {
    io_service_->work_finished();
  }

  win_iocp_io_service * const io_service_;
};

struct win_iocp_io_service::timer_thread_function
{
  void operator()()
  {
    while (::InterlockedExchangeAdd(&io_service_->shutdown_, 0) == 0)
    {
      if (::WaitForSingleObject(io_service_->waitable_timer_.handle,
            INFINITE) == WAIT_OBJECT_0)
      {
				io_service_->dispatch_required_.store(true);
        ::PostQueuedCompletionStatus(io_service_->iocp_.handle,
            0, wake_for_dispatch, 0);
      }
    }
  }

  win_iocp_io_service * const io_service_;
};

win_iocp_io_service::win_iocp_io_service(
    boost::asio::io_service& io_service, size_t concurrency_hint)
  : boost::asio::detail::service_base<win_iocp_io_service>(io_service),
    iocp_(),
    stop_event_posted_(0),
    shutdown_(0),
    outstanding_work_(0)
{
  BOOST_ASIO_HANDLER_TRACKING_INIT;

  iocp_.handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0,
      static_cast<DWORD>(concurrency_hint < DWORD(~0)
        ? concurrency_hint : DWORD(~0)));
  if (!iocp_.handle)
  {
    DWORD last_error = ::GetLastError();
    boost::system::error_code ec(last_error,
        boost::asio::error::get_system_category());
    boost::asio::detail::throw_error(ec, "iocp");
  }
}

void win_iocp_io_service::shutdown_service()
{
  ::InterlockedExchange(&shutdown_, 1);

	#ifdef BOOST_HAS_THREADS
  if (timer_thread_.get())
	#else
	if (waitable_timer_.handle)
	#endif
  {
    LARGE_INTEGER timeout;
    timeout.QuadPart = 1;
    ::SetWaitableTimer(waitable_timer_.handle, &timeout, 1, 0, 0, FALSE);
  }

  while (::InterlockedExchangeAdd(&outstanding_work_, 0) > 0)
  {
    op_queue<win_iocp_operation> ops;
    timer_queues_.get_all_timers(ops);
    ops.push(completed_ops_);
    if (!ops.empty())
    {
      while (win_iocp_operation* op = ops.front())
      {
        ops.pop();
        ::InterlockedDecrement(&outstanding_work_);
        op->destroy();
      }
    }
    else
    {
      DWORD bytes_transferred = 0;
      dword_ptr_t completion_key = 0;
      LPOVERLAPPED overlapped = 0;
      ::GetQueuedCompletionStatus(iocp_.handle, &bytes_transferred,
          &completion_key, &overlapped, INFINITE);
      if (overlapped)
      {
        ::InterlockedDecrement(&outstanding_work_);
        static_cast<win_iocp_operation*>(overlapped)->destroy();
      }
    }
  }

	#ifdef BOOST_HAS_THREADS
  if (timer_thread_.get())
    timer_thread_->join();
	#endif
}

boost::system::error_code win_iocp_io_service::register_handle(
    HANDLE handle, boost::system::error_code& ec)
{
  if (::CreateIoCompletionPort(handle, iocp_.handle, 0, 0) == 0)
  {
    DWORD last_error = ::GetLastError();
    ec = boost::system::error_code(last_error,
        boost::asio::error::get_system_category());
  }
  else
  {
    ec = boost::system::error_code();
  }
  return ec;
}

void win_iocp_io_service::run() {

  if (::InterlockedExchangeAdd(&outstanding_work_, 0) == 0) {
    stop();
    return;
  }

  win_iocp_thread_info this_thread;
  thread_call_stack::context ctx(this, this_thread);

	Completion_Statuses_Accumulator_Type Completion_Statuses_Accumulator(*this);

  for (;;) {
    // Try to acquire responsibility for dispatching timers and completed ops.
		#ifndef BOOST_HAS_THREADS
			if (!Busy_Poll)
				::std::terminate(); // todo more elegant 'this is unsupported' unwinding and notification
			if (waitable_timer_.handle && ::WaitForSingleObject(waitable_timer_.handle, 0) == WAIT_OBJECT_0) {
				dispatch_required_.store(true);
        ::PostQueuedCompletionStatus(iocp_.handle, 0, wake_for_dispatch, 0);
			}
		#endif
		bool Expected(true);
    if (::std::atomic_compare_exchange_strong(&dispatch_required_, &Expected, false))
    {
      mutex::scoped_lock lock(dispatch_mutex_);

      // Dispatch pending timers and operations.
      op_queue<win_iocp_operation> ops;
      ops.push(completed_ops_);
      timer_queues_.get_ready_timers(ops);
      post_deferred_completions(ops);
      update_timeout();
    }

    // Get the next operation from the queue.
		::OVERLAPPED_ENTRY * Overlapped_Entry(nullptr);
		BOOL ok(TRUE);
		DWORD last_error(0);
		if (Completion_Statuses_Accumulator.Current_Completion_Status_Index != Completion_Statuses_Accumulator.Completion_Statuses_Size) 
			Overlapped_Entry = Completion_Statuses_Accumulator.Completion_Statuses + Completion_Statuses_Accumulator.Current_Completion_Status_Index++;
		else {
			::SetLastError(0);
			ok = ::GetQueuedCompletionStatusEx(iocp_.handle, Completion_Statuses_Accumulator.Completion_Statuses, Completion_Statuses_Accumulator.Completion_Statuses_Capacity, &Completion_Statuses_Accumulator.Completion_Statuses_Size, !Busy_Poll ? INFINITE : 0, FALSE);
			last_error = ::GetLastError();
			if (ok && Completion_Statuses_Accumulator.Completion_Statuses_Size) {
				Completion_Statuses_Accumulator.Current_Completion_Status_Index = 1;
				Overlapped_Entry = Completion_Statuses_Accumulator.Completion_Statuses;
			} else
				Completion_Statuses_Accumulator.Completion_Statuses_Size = Completion_Statuses_Accumulator.Current_Completion_Status_Index = 0;
		}

    if (Overlapped_Entry != nullptr && Overlapped_Entry->lpOverlapped) {
			win_iocp_operation * const op(static_cast<win_iocp_operation*>(Overlapped_Entry->lpOverlapped));
			boost::system::error_code result_ec(ok ? op->Internal : last_error,
					boost::asio::error::get_system_category());

			// We may have been passed the last_error and bytes_transferred in the
			// OVERLAPPED structure itself.
			if (Overlapped_Entry->lpCompletionKey == overlapped_contains_result)
			{
				result_ec = boost::system::error_code(static_cast<int>(op->Offset),
						*reinterpret_cast<boost::system::error_category*>(op->Internal));
			}

			// Otherwise ensure any result has been saved into the OVERLAPPED
			// structure.
			else
			{
				op->Internal = reinterpret_cast<ulong_ptr_t>(&result_ec.category());
				op->Offset = result_ec.value();
				op->OffsetHigh = Overlapped_Entry->dwNumberOfBytesTransferred;
			}

			// Dispatch the operation only if ready. The operation may not be ready
			// if the initiating function (e.g. a call to WSARecv) has not yet
			// returned. This is because the initiating function still wants access
			// to the operation's OVERLAPPED structure.
			if (::InterlockedCompareExchange(&op->ready_, 1, 0) == 1)
			{
				// Ensure the count of outstanding work is decremented on block exit.
				work_finished_on_block_exit on_exit = { this };
				(void)on_exit;

				op->complete(*this, result_ec, op->OffsetHigh);
			}
    } else if (!ok) {
      if (last_error != WAIT_TIMEOUT)
				boost::asio::detail::throw_exception(boost::system::system_error(boost::system::error_code(last_error, boost::asio::error::get_system_category())));
    } else if (Overlapped_Entry == nullptr || Overlapped_Entry->lpCompletionKey != wake_for_dispatch) {
			// Indicate that there is no longer an in-flight stop event.
			::InterlockedExchange(&stop_event_posted_, 0);

			// The stopped_ flag is always checked to ensure that any leftover
			// stop events from a previous run invocation are ignored.
			if (stopped_.load()) {
				// Wake up next thread that is blocked on GetQueuedCompletionStatus.
				if ( ::InterlockedExchange(&stop_event_posted_, 1) == 0 && !::PostQueuedCompletionStatus(iocp_.handle, 0, 0, 0))
					boost::asio::detail::throw_exception(boost::system::system_error(boost::system::error_code(::GetLastError(), boost::asio::error::get_system_category())));
				else
					return;
			}
		}
  }
}

void win_iocp_io_service::stop()
{
  if (!stopped_.exchange(true))
  {
    if (::InterlockedExchange(&stop_event_posted_, 1) == 0)
    {
      if (!::PostQueuedCompletionStatus(iocp_.handle, 0, 0, 0))
      {
        DWORD last_error = ::GetLastError();
        boost::system::error_code ec(last_error,
            boost::asio::error::get_system_category());
        boost::asio::detail::throw_error(ec, "pqcs");
      }
    }
  }
}

void win_iocp_io_service::post_deferred_completion(win_iocp_operation* op)
{
  // Flag the operation as ready.
  op->ready_ = 1;

  // Enqueue the operation on the I/O completion port.
  if (!::PostQueuedCompletionStatus(iocp_.handle, 0, 0, op))
  {
    // Out of resources. Put on completed queue instead.
    mutex::scoped_lock lock(dispatch_mutex_);
    completed_ops_.push(op);
		dispatch_required_.store(true);
  }
}

void win_iocp_io_service::post_deferred_completions(
    op_queue<win_iocp_operation>& ops)
{
  while (win_iocp_operation* op = ops.front())
  {
    ops.pop();

    // Flag the operation as ready.
    op->ready_ = 1;

    // Enqueue the operation on the I/O completion port.
    if (!::PostQueuedCompletionStatus(iocp_.handle, 0, 0, op))
    {
      // Out of resources. Put on completed queue instead.
      mutex::scoped_lock lock(dispatch_mutex_);
      completed_ops_.push(op);
      completed_ops_.push(ops);
			dispatch_required_.store(true);
    }
  }
}

void win_iocp_io_service::abandon_operations(
    op_queue<win_iocp_operation>& ops)
{
  while (win_iocp_operation* op = ops.front())
  {
    ops.pop();
    ::InterlockedDecrement(&outstanding_work_);
    op->destroy();
  }
}

void win_iocp_io_service::on_pending(win_iocp_operation* op)
{
  if (::InterlockedCompareExchange(&op->ready_, 1, 0) == 1)
  {
    // Enqueue the operation on the I/O completion port.
    if (!::PostQueuedCompletionStatus(iocp_.handle,
          0, overlapped_contains_result, op))
    {
      // Out of resources. Put on completed queue instead.
      mutex::scoped_lock lock(dispatch_mutex_);
      completed_ops_.push(op);
			dispatch_required_.store(true);
    }
  }
}

void win_iocp_io_service::on_completion(win_iocp_operation* op,
    DWORD last_error, DWORD bytes_transferred)
{
  // Flag that the operation is ready for invocation.
  op->ready_ = 1;

  // Store results in the OVERLAPPED structure.
  op->Internal = reinterpret_cast<ulong_ptr_t>(
      &boost::asio::error::get_system_category());
  op->Offset = last_error;
  op->OffsetHigh = bytes_transferred;

  // Enqueue the operation on the I/O completion port.
  if (!::PostQueuedCompletionStatus(iocp_.handle,
        0, overlapped_contains_result, op))
  {
    // Out of resources. Put on completed queue instead.
    mutex::scoped_lock lock(dispatch_mutex_);
    completed_ops_.push(op);
		dispatch_required_.store(true);
  }
}

void win_iocp_io_service::on_completion(win_iocp_operation* op,
    const boost::system::error_code& ec, DWORD bytes_transferred)
{
  // Flag that the operation is ready for invocation.
  op->ready_ = 1;

  // Store results in the OVERLAPPED structure.
  op->Internal = reinterpret_cast<ulong_ptr_t>(&ec.category());
  op->Offset = ec.value();
  op->OffsetHigh = bytes_transferred;

  // Enqueue the operation on the I/O completion port.
  if (!::PostQueuedCompletionStatus(iocp_.handle,
        0, overlapped_contains_result, op))
  {
    // Out of resources. Put on completed queue instead.
    mutex::scoped_lock lock(dispatch_mutex_);
    completed_ops_.push(op);
		dispatch_required_.store(true);
  }
}

void win_iocp_io_service::do_add_timer_queue(timer_queue_base& queue)
{
  mutex::scoped_lock lock(dispatch_mutex_);

  timer_queues_.insert(&queue);

  if (!waitable_timer_.handle)
  {
    waitable_timer_.handle = ::CreateWaitableTimer(0, FALSE, 0);
    if (waitable_timer_.handle == 0)
    {
      DWORD last_error = ::GetLastError();
      boost::system::error_code ec(last_error,
          boost::asio::error::get_system_category());
      boost::asio::detail::throw_error(ec, "timer");
    }

    LARGE_INTEGER timeout;
    timeout.QuadPart = -max_timeout_usec;
    timeout.QuadPart *= 10;
    ::SetWaitableTimer(waitable_timer_.handle,
        &timeout, max_timeout_msec, 0, 0, FALSE);
  }

	#ifdef BOOST_HAS_THREADS
		if (!timer_thread_.get())
		{
			timer_thread_function thread_function = { this };
			timer_thread_.reset(new thread(thread_function, 65536));
		}
	#endif
}

void win_iocp_io_service::do_remove_timer_queue(timer_queue_base& queue)
{
  mutex::scoped_lock lock(dispatch_mutex_);

  timer_queues_.erase(&queue);
}

void win_iocp_io_service::update_timeout()
{
	#ifdef BOOST_HAS_THREADS
	if (timer_thread_.get())
	#else
	if (waitable_timer_.handle)
	#endif
	{
		// There's no point updating the waitable timer if the new timeout period
		// exceeds the maximum timeout. In that case, we might as well wait for the
		// existing period of the timer to expire.
		long timeout_usec = timer_queues_.wait_duration_usec(max_timeout_usec);
		if (timeout_usec < max_timeout_usec)
		{
			LARGE_INTEGER timeout;
			timeout.QuadPart = -timeout_usec;
			timeout.QuadPart *= 10;
			::SetWaitableTimer(waitable_timer_.handle,
					&timeout, max_timeout_msec, 0, 0, FALSE);
		}
	}
}

} // namespace detail
} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // defined(BOOST_ASIO_HAS_IOCP)

#endif // BOOST_ASIO_DETAIL_IMPL_WIN_IOCP_IO_SERVICE_IPP
