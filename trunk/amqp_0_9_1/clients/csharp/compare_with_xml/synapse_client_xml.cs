using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using RabbitMQ.Client;
using RabbitMQ.Client.Events;
using RabbitMQ.Util;
using RabbitMQ.Client.MessagePatterns;
using System.Xml.Serialization;

namespace data_processors { namespace xml {

public class message_base {
	public virtual uint get_type_id(){return uint.MaxValue;}
}

public interface message_factory_base {
	message_base from_type_id(uint id);
}

public class type_factory : data_processors.xml.message_factory_base { 
	public data_processors.xml.message_base
	from_type_id(uint id)
	{
		switch (id) {
            case 0: return new ConsoleApplication1.imaginary_bet_pool_xml();
			} return null;
	}
}
		
public class message_wrapper {
	public message_base msg;
	public bool non_delta_seen = false;
	public message_wrapper(message_base msg_) {
		msg = msg_;
	}
};

public class synapse_client {

	message_factory_base message_factory;
	ConnectionFactory factory;
	IConnection conn;
	IModel ch = null; // subscribing
	IModel publishing_channel = null; // publishing (class is a bit of a duplex(ish) client)
	QueueingBasicConsumer consumer;

	public static DateTime epoch = new DateTime(1970, 1, 1);
	IBasicProperties props = null;


	// read message's properties
	BasicDeliverEventArgs amqp_msg;
	public long msg_timestamp;
	public long msg_sequence_number;

	public Dictionary<string, Dictionary<uint, message_wrapper>> previous_messages = new Dictionary<string, Dictionary<uint, message_wrapper>>();

	public synapse_client(message_factory_base message_factory_, string server_host = "localhost", int server_port = 5672) 
	{
		message_factory = message_factory_;

		// AMQP connection
		factory = new ConnectionFactory();
		factory.HostName = server_host;
		factory.Port = server_port;
		conn = factory.CreateConnection();
		ch = conn.CreateModel();
		conn.AutoClose = true;
		consumer = new QueueingBasicConsumer(ch);
		publishing_channel = conn.CreateModel();
		props = publishing_channel.CreateBasicProperties();

	}

	public void
		close()
		{
			// ... done
			if (ch != null)
				ch.Close();
			if (publishing_channel != null)
				publishing_channel.Close();
		}

	~synapse_client()
	{
		// TODO -- may not be safe to call twice: if so, protect with a flag/bool
		close();
	}

	public void
		subscribe(string topic_name, DateTime? begin_utc = null, DateTime? end_utc = null, bool supply_metadata = false)
		{
			var dictionary = new Dictionary<string, object>();

			if (begin_utc != null)
				dictionary.Add("from_seconds_since_epoch", (int)(begin_utc.Value - epoch).TotalSeconds);

			if (end_utc != null)
				dictionary.Add("until_seconds_since_epoch", (int)(end_utc.Value - epoch).TotalSeconds);

			if (supply_metadata == true)
				dictionary.Add("supply_metadata", (int)1);

			// not too fast at the moment (bare bones example), TODO later will set topic_name elsewhere so as not to resend it everytime
			ch.BasicConsume(topic_name, true, "", dictionary, consumer);
		}

	public message_wrapper
		next()
		{
			message_wrapper rv = null;
			amqp_msg = (BasicDeliverEventArgs)consumer.Queue.Dequeue();
			if (amqp_msg.Body.Length != 0) {

				MemoryStream stream = new MemoryStream(amqp_msg.Body);

				var bytes = new byte[4];
				stream.Read(bytes, 0, 4);
				if (BitConverter.IsLittleEndian)
					Array.Reverse(bytes);
				uint type_id = (uint)BitConverter.ToInt32(bytes, 0);

				var tmp = message_factory.from_type_id(type_id);
                var xmlSerializer = new XmlSerializer(tmp.GetType());
                rv = new message_wrapper((message_base)xmlSerializer.Deserialize(stream));
			} 
			return rv;
		}

	public void
		decode_timestamp_and_sequence_number()
		{
			msg_timestamp = amqp_msg.BasicProperties.Timestamp.UnixTime;
			msg_sequence_number = (long)amqp_msg.BasicProperties.Headers["XXXXXX"];
		}
    public MemoryStream stream;
	public void
		publish(string topic_name, message_base msg, long message_sequence_number = 0, long micros_since_epoch = 0) 
		{
            byte[] bytes = BitConverter.GetBytes(msg.get_type_id());
			if (BitConverter.IsLittleEndian)
				Array.Reverse(bytes);
			stream = new MemoryStream();
			stream.Write(bytes, 0, 4);
			var xmlSerializer = new XmlSerializer(msg.GetType());
			xmlSerializer.Serialize(stream, msg);

			// explicit since epoch in microseconds time for the server's requirements
			if (micros_since_epoch != 0 && message_sequence_number != 0) {
				var dictionary = new Dictionary<string, object>();
				dictionary.Add("XXXXXX", message_sequence_number);
				props.ContentType = "X";
				props.Headers = dictionary;
				props.Timestamp = new AmqpTimestamp(micros_since_epoch);
				publishing_channel.BasicPublish("", topic_name, props, stream.ToArray());
			} else 
				publishing_channel.BasicPublish("", topic_name, null, stream.ToArray());

           // Console.WriteLine("xml size: " + stream.ToArray().Length.ToString());
        }

	public long
		timestamp_now()
		{
			var since_epoch = DateTime.UtcNow - epoch;
			return (long)since_epoch.TotalMilliseconds * 1000L;
		}
}
}}
