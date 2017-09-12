#ifndef DATA_PROCESSORS_SYNAPSE_MISC_SOCKET_H
#define DATA_PROCESSORS_SYNAPSE_MISC_SOCKET_H

#include <stdexcept>
#include <string>

#ifdef __WIN32__
	#include <Windows.h>
	#include <mstcpip.h>
	#if defined(__MINGW64__)
		#ifndef SIO_LOOPBACK_FAST_PATH
			#include <ws2bth.h>
			#define SIO_LOOPBACK_FAST_PATH _WSAIOW(IOC_VENDOR, 16)
		#endif
	#endif
#endif


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc {

#ifdef __WIN32__
void static Enable_Tcp_Loopback_Fast_Path(SOCKET Socket) {
	int True_value(1); DWORD Unused_number_of_bytes_returned(0);
	if (::WSAIoctl(Socket, SIO_LOOPBACK_FAST_PATH, &True_value, sizeof(True_value), NULL, 0, &Unused_number_of_bytes_returned, 0, 0) == SOCKET_ERROR)
		throw ::std::runtime_error("Tcp_loopback_fast_path(=SIO_LOOPBACK_FAST_PATH)), Error code(=" + ::std::to_string(::GetLastError()) + ')');
}
#endif

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif

