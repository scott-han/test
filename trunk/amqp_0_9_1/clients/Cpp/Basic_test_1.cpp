#include <iostream>
#include <boost/make_shared.hpp>
#include <thrift/Thrift.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TCompactProtocol.h>
#include <data_processors/Federated_serialisation/Schemas/contests4_types.h>
#include <data_processors/Federated_serialisation/Utils.h>

::DataProcessors::Contests4::Contests static To_send, Received;

void static Roundtrip(bool Send_as_delta) {
	::std::clog << "Sending delta: " << Send_as_delta << "\n";
	typedef ::apache::thrift::transport::TMemoryBuffer Buffer_type;
	typedef ::apache::thrift::protocol::TCompactProtocol Protocol_type;

	auto Obtained_from_wire(::boost::make_shared<Buffer_type>());

	{ // as if writing (to wire)
		auto Buffer(::boost::make_shared<Buffer_type>());
		auto Protocol(::boost::make_shared<Protocol_type>(Buffer));

		To_send.write(Protocol.get(), !Send_as_delta);

		char unsigned * Raw_buffer;
		uint32_t Raw_buffer_size;
		Buffer->getBuffer(&Raw_buffer, &Raw_buffer_size);

		::std::clog << "Ready to output buffer at 0x" << ::std::hex << reinterpret_cast<uintptr_t>(Raw_buffer) << " to some networking layer as a payload of " << ::std::dec << Raw_buffer_size << " bytes." << ::std::endl;

		Obtained_from_wire->resetBuffer(Raw_buffer, Raw_buffer_size, ::apache::thrift::transport::TMemoryBuffer::MemoryPolicy::COPY);
	}

	{ // as if reading (from wire)
		auto Protocol(::boost::make_shared<Protocol_type>(Obtained_from_wire));

		Received.Read_in_delta_mode = Send_as_delta;
		Received.read(Protocol.get());

		if (Received.Is_datasource_set())
			::std::clog << "Read datasource as: " << Received.Get_datasource() << ::std::endl;
	}
}

int main() {
	To_send.Set_datasource("Betfair la di da");
	Roundtrip(false);
	assert(To_send == Received);
	To_send.Set_dataprovider("blam super duper");
	Roundtrip(true);
	assert(To_send == Received);
	Roundtrip(false);
	assert(To_send == Received);
	auto const Contest_key("abobeb");
	auto && Contest(To_send.Set_contest_element(Contest_key));
	Contest.Set_competition("Super fast race crown thingy");
	Roundtrip(false);
	assert(To_send == Received);
	Roundtrip(true);
	assert(To_send == Received);
	for (unsigned i(0); i != 100; ++i) {
		auto && Market(Contest.Set_markets_element("r8_win_something"));
		for (uint8_t j(0); j != 8; ++j) {
			Market.Set_prices_element(::std::string(1, j + 1), 0.1 + ::rand() / static_cast<double>(RAND_MAX / 50));
		}
	}
	Roundtrip(true);
	assert(To_send == Received);
	To_send.Set_datasource("Betfair la di da indeed.");
	Roundtrip(true);
	assert(To_send == Received);
	::std::string const Test_timestamp("20010203T050708.123000");
	::DataProcessors::Shared::Timestamp tmp; 
	tmp.Set_value(::data_processors::Federated_serialisation::Utils::Encode_timestamp(::boost::posix_time::from_iso_string(Test_timestamp)));
	Contest.Set_scheduledStart(::std::move(tmp));
	Roundtrip(true);
	assert(To_send == Received);
	assert(Received.Is_contest_set());
	{
		auto & Contest(Received.Get_contest_element(Contest_key));
		assert(Contest.Is_scheduledStart_set());
		auto && Scheduled_start(Contest.Get_scheduledStart());
		assert(Scheduled_start.Is_value_set());
		assert(to_iso_string(::data_processors::Federated_serialisation::Utils::Decode_timestamp_to_ptime(Scheduled_start.Get_value())) == Test_timestamp);
	}
	return 0;
}

