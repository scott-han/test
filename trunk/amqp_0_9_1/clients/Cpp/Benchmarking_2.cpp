
// Todo: move it elsewhere (not really a part of synapse, amqp, thrift at all)...

// Essentially a copy from
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms737593(v=vs.85).aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms737591(v=vs.85).aspx

#undef UNICODE

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include <vector>
#include <iostream>
#include <cstring>

#include "../../../misc/Stop_Watch.h"
#include "../../../misc/Socket.h"
#include "../../../misc/misc.h"

// Todo: link with:
// Ws2_32.lib
// Mswsock.lib
// AdvApi32.lib

namespace {


::std::string static host("localhost");
::std::string static port("5672");

unsigned static Total_Messages_Size{0};

// 100 bytes (similar to Benchmarking_1 in the simplest of use-cases.
char static Payload[] = 
"1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
;
char static Output_Buffer[sizeof(Payload)];
// Todo: may be add some string suffix with timestamp (to mimic golang?)
char static Input_Buffer[sizeof(Payload)];

struct Scoped_Address_Info_Type {
	::addrinfo * result{NULL};
	Scoped_Address_Info_Type() {
		struct addrinfo Hints;
		::ZeroMemory(&Hints, sizeof(Hints));
		Hints.ai_family = AF_UNSPEC;
		Hints.ai_socktype = SOCK_STREAM;
		Hints.ai_protocol = IPPROTO_TCP;
		if (getaddrinfo(host.c_str(), port.c_str(), &Hints, &result) != 0 )
			throw ::std::runtime_error("getaddrinfo");
	}
	~Scoped_Address_Info_Type() {
		if (result != NULL)
			::freeaddrinfo(result);
	}
};

struct Scoped_Socket_Type {
	::SOCKET Socket{INVALID_SOCKET};
	~Scoped_Socket_Type() {
		if (Socket != INVALID_SOCKET)
			::closesocket(Socket);
	}
	void Set_Options() {
		u_long Non_Blocking_On(1);
		if (::ioctlsocket(Socket, FIONBIO, &Non_Blocking_On) != NO_ERROR)
			throw ::std::runtime_error("ioctlsocket");
		char value(1);
    if (::setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, &value, 1))
			throw ::std::runtime_error("setsockopt");
	}
};

struct Scoped_Winsock_System_Type {
	WSADATA wsaData;
	Scoped_Winsock_System_Type() {
		if (::WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
			throw ::std::runtime_error("WSAStartup");
	}
	~Scoped_Winsock_System_Type() {
		::WSACleanup();
	}
};

template <typename IO_Type, IO_Type IO>
int static Perform_IO(::SOCKET & Socket, char * const Buffer) {
	for (unsigned i(0); i != sizeof(Payload);) {
		auto Bytes_Done(IO(Socket, Buffer + i, sizeof(Payload) - i, 0));
		if (Bytes_Done == SOCKET_ERROR) {
			auto const Error(::WSAGetLastError());
			if (Error == WSAEWOULDBLOCK)
				continue;
			return Error;
		}
		i += Bytes_Done;
	}
	return 0;
}

void static Send(::SOCKET & Socket) {
	auto const Error_Code(Perform_IO<decltype(&::send), ::send>(Socket, Output_Buffer));
	if (Error_Code)
		throw ::std::runtime_error("Send (=" + ::std::to_string(Error_Code) + ')');
}

void static Receive(::SOCKET & Socket) {
	auto const Error_Code(Perform_IO<decltype(&::recv), ::recv>(Socket, Input_Buffer) == SOCKET_ERROR);
	if (Error_Code)
		throw ::std::runtime_error("Receive (=" + ::std::to_string(Error_Code) + ')');
}

	
void static Run_As_Server() {
	Scoped_Winsock_System_Type Scoped_Winsock_System;

	Scoped_Socket_Type Client_Socket;

	{
		Scoped_Socket_Type Listening_Socket;
		{
			Scoped_Address_Info_Type Scoped_Address_Info;

			Listening_Socket.Socket = ::socket(Scoped_Address_Info.result->ai_family, Scoped_Address_Info.result->ai_socktype, Scoped_Address_Info.result->ai_protocol);
			if (Listening_Socket.Socket == INVALID_SOCKET) 
				throw ::std::runtime_error("socket");

			::data_processors::synapse::misc::Enable_Tcp_Loopback_Fast_Path(Listening_Socket.Socket);

			if (bind(Listening_Socket.Socket, Scoped_Address_Info.result->ai_addr, (int)Scoped_Address_Info.result->ai_addrlen) == SOCKET_ERROR) 
				throw ::std::runtime_error("bind");
		}

		if (::listen(Listening_Socket.Socket, SOMAXCONN) == SOCKET_ERROR) 
			throw ::std::runtime_error("listen");

		// Accept a client socket
		Client_Socket.Socket = ::accept(Listening_Socket.Socket, NULL, NULL);
		if (Client_Socket.Socket == INVALID_SOCKET)
			throw ::std::runtime_error("accept");
	}

	Client_Socket.Set_Options();
	// Todo: more elegance
	while (!0) {
		Receive(Client_Socket.Socket);
		::std::memcpy(Output_Buffer, Input_Buffer, sizeof(Payload));
		Send(Client_Socket.Socket);
	}
}

void static Run_As_Client() {

	::std::memcpy(Output_Buffer, Payload, sizeof(Payload));
	Scoped_Winsock_System_Type Scoped_Winsock_System;

	Scoped_Socket_Type Scoped_Socket;

	{
		Scoped_Address_Info_Type Scoped_Address_Info;

		for(::addrinfo * ptr(Scoped_Address_Info.result); ptr != NULL ; ptr=ptr->ai_next) {

			Scoped_Socket.Socket = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
			if (Scoped_Socket.Socket == INVALID_SOCKET)
				throw ::std::runtime_error("socket");

			::data_processors::synapse::misc::Enable_Tcp_Loopback_Fast_Path(Scoped_Socket.Socket);
			if (::connect(Scoped_Socket.Socket, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
					::closesocket(Scoped_Socket.Socket);
					Scoped_Socket.Socket = INVALID_SOCKET;
					continue;
			}
			break;
		}
	}

	if (Scoped_Socket.Socket == INVALID_SOCKET)
		throw ::std::runtime_error("socket");

	Scoped_Socket.Set_Options();

	::std::vector<int_fast64_t> Subscriber_Statistics;
	Subscriber_Statistics.reserve(Total_Messages_Size);
	::data_processors::synapse::misc::Stopwatch_Type Stopwatch;
	for (unsigned i(0); i != Total_Messages_Size; ++i) {
		auto const Stopwatch_Begin_Ping(Stopwatch.Ping());
		Send(Scoped_Socket.Socket);
		Receive(Scoped_Socket.Socket);
		Subscriber_Statistics.emplace_back(Stopwatch.Get_Delta_As_Microseconds_With_Auto_Correction(Stopwatch_Begin_Ping, Stopwatch.Ping()));

		// Scheduler-based sleep (likely to switch-out the thread)
		//::Sleep(100);
		// Busy-pollyng (not liekly to switch-out the thread on sufficiently multi-core system)
		//{
		//	auto const Begin(Stopwatch.Ping());
		//	while (Stopwatch.Get_Delta_As_Microseconds_With_Auto_Correction(Begin, Stopwatch.Ping()) < 100000);
		//}
	}
	for (auto const & Point : Subscriber_Statistics)
		::std::cout << Point << ::std::endl;
}

int static Run(int argc, char* argv[]) {
	enum struct Action {None, Client, Server};
	Action Run_as{Action::None};
	uint_fast64_t Processor_Affinity(0);
	try {
		// CLI...
		for (int i(1); i != argc; ++i)
			if (i == argc - 1)
				throw ::std::runtime_error(::std::string(argv[i]) + " ought to have an explicit value");
			else if (!::data_processors::synapse::misc::CLI_Option_Parse_Host_Port("--Server_At", i, argv, host, port))
			if (!::strcmp(argv[i], "--Action")) {
				++i;
				if (!::strcmp(argv[i], "Client"))
					Run_as = Action::Client;
				else if (!::strcmp(argv[i], "Server"))
					Run_as = Action::Server;
				else
					throw ::std::runtime_error("unknown --Action value(=" + ::std::string(argv[i]) + ')');
			} else if (!::strcmp(argv[i], "--processor_affinity"))
				Processor_Affinity = ::std::stoll(argv[++i]);
			else if (!::strcmp(argv[i], "--Total_Messages_Size"))
				Total_Messages_Size = ::std::stoll(argv[++i]);
			else
				throw ::std::runtime_error("unknown option(=" + ::std::string(argv[i]) + ')');

		if (Run_as == Action::None)
			throw ::std::runtime_error("--Action option must be specified");

		if (Processor_Affinity) {
			uint64_t Unused;
			uint64_t System_Mask;
			auto const Current_Process(::GetCurrentProcess());
			if (!::GetProcessAffinityMask(Current_Process, &Unused, &System_Mask))
				throw ::std::runtime_error("GetProcessAffinityMask failed");
			if (~System_Mask & Processor_Affinity)
				throw ::std::runtime_error("Supplied Mask is conflicting with system-allowed affinity (must be a subset)");
			if (!::SetProcessAffinityMask(Current_Process, Processor_Affinity))
				throw ::std::runtime_error("SetProcessAffinityMask failed");
		}

		if (!::SetPriorityClass(::GetCurrentProcess(), REALTIME_PRIORITY_CLASS) || !::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
			throw ::std::runtime_error("SetPriorityClass(=REALTIME_PRIORITY_CLASS) || SetThreadPriority(=THREAD_PRIORITY_TIME_CRITICAL)");

		if (Run_as == Action::Client) {
			if (!Total_Messages_Size)
				throw ::std::runtime_error("--Total_Messages_Size option must be specified and should be > 0");
			Run_As_Client();
		} else
			Run_As_Server();

		return 0;
	}
	catch (::std::exception const & e)
	{
		::std::cerr << "oops: " << e.what() << ::std::endl;
		return -1;
	}
}

}


int main(int argc, char* argv[])
{
	return Run(argc, argv);
}
