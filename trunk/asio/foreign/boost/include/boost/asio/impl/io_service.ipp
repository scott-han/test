//
// impl/io_service.ipp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_IMPL_IO_SERVICE_IPP
#define BOOST_ASIO_IMPL_IO_SERVICE_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/detail/limits.hpp>
#include <boost/asio/detail/scoped_ptr.hpp>
#include <boost/asio/detail/service_registry.hpp>
#include <boost/asio/detail/throw_error.hpp>

#if defined(BOOST_ASIO_HAS_IOCP)
# include <boost/asio/detail/win_iocp_io_service.hpp>
#else
# include <boost/asio/detail/task_io_service.hpp>
#endif

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {

io_service::io_service()
  : service_registry_(new boost::asio::detail::service_registry(
        *this, static_cast<impl_type*>(0),
        (std::numeric_limits<std::size_t>::max)())),
    impl_(service_registry_->first_service<impl_type>())
{
}

io_service::io_service(std::size_t concurrency_hint)
  : service_registry_(new boost::asio::detail::service_registry(
        *this, static_cast<impl_type*>(0), concurrency_hint)),
    impl_(service_registry_->first_service<impl_type>())
{
}

io_service::~io_service()
{
  delete service_registry_;
}

void io_service::run()
{
  impl_.run();
}

void io_service::stop()
{
  impl_.stop();
}

bool io_service::stopped() const
{
  return impl_.stopped();
}

void io_service::reset()
{
  impl_.reset();
}

void io_service::notify_fork(boost::asio::io_service::fork_event event)
{
  service_registry_->notify_fork(event);
}

io_service::service::service(boost::asio::io_service& owner)
  : owner_(owner),
    next_(0)
{
}

io_service::service::~service()
{
}

void io_service::service::fork_service(boost::asio::io_service::fork_event)
{
}

service_already_exists::service_already_exists()
  : std::logic_error("Service already exists.")
{
}

invalid_service_owner::invalid_service_owner()
  : std::logic_error("Invalid service owner.")
{
}

} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_ASIO_IMPL_IO_SERVICE_IPP
