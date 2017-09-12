#ifndef TOPIC_MIRROR_PCH_H
#define TOPIC_MIRROR_PCH_H

#include <iostream>
#include <fstream>
#include <atomic>
#include <thread>

#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>

#define WINDOWS_EVENT_LOGGING_SOURCE_ID "Topic_mirror"
#include "Basic_client_1.h"

#include "../../../asio/io_service.h"
#include "../../../misc/sysinfo.h"

#include <openssl/sha.h>
#include <openssl/md5.h>

#endif
