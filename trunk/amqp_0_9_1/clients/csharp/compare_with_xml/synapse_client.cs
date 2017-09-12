using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using Thrift;
using Thrift.Protocol;
using Thrift.Transport;
using RabbitMQ.Client;
using RabbitMQ.Client.Events;
using RabbitMQ.Util;
using RabbitMQ.Client.MessagePatterns;

namespace data_processors {

public interface user_data_interface {
	void on_modified();
	void on_deleted();
}

public class message_base {
	public bool read_in_delta_mode = false;
	public virtual void Read(TProtocol tProtocol){}
	public virtual void Write(TProtocol tProtocol){}
	public virtual uint get_type_id(){return uint.MaxValue;}
	public virtual void set_modified_flag() {}
	public virtual void clear_modified_flag() {}
}

public interface message_factory_base {
	message_base from_type_id(uint id);
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
	Dictionary<string, IModel> publishing_channels = new Dictionary<string, IModel>(); // publishing (class is a bit of a duplex(ish) client)
	QueueingBasicConsumer consumer;

	public static DateTime epoch = new DateTime(1970, 1, 1);
	IBasicProperties props = null;


	// read message's properties
	BasicDeliverEventArgs amqp_msg;
	public long msg_timestamp;
	public long msg_sequence_number;

	public Dictionary<string, Dictionary<uint, Dictionary<int, message_wrapper>>> previous_messages = new Dictionary<string, Dictionary<uint, Dictionary<int, message_wrapper>>>();

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

	}

	public void
		close()
		{
			// ... done
			foreach (var pub_i in publishing_channels)
				pub_i.Value.Close();
			if (ch != null)
				ch.Close();
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
				var proto = new TCompactProtocol(new TStreamTransport(stream, stream));

				int delta_stream_id = (int)proto.ReadI32();
				bool read_in_delta_mode;
				if (delta_stream_id < 0) {
					read_in_delta_mode = false;
					delta_stream_id = -delta_stream_id;
				} else
					read_in_delta_mode = true;

				uint type_id = (uint)proto.ReadI32();

				Dictionary<uint, Dictionary<int, message_wrapper>> old_subject = null; 
				if (!previous_messages.TryGetValue(amqp_msg.RoutingKey, out old_subject)) {
					var deltas = new Dictionary<int, message_wrapper>();
					deltas[delta_stream_id] = new message_wrapper(message_factory.from_type_id(type_id));
					old_subject = new Dictionary<uint, Dictionary<int, message_wrapper>>();
					old_subject[type_id] = deltas;
					previous_messages[amqp_msg.RoutingKey] = old_subject;
				}

				Dictionary<int, message_wrapper> old_type_id = null; 
				if (!old_subject.TryGetValue(type_id, out old_type_id)) {
					var deltas = new Dictionary<int, message_wrapper>();
					deltas[delta_stream_id] = new message_wrapper(message_factory.from_type_id(type_id));
					old_subject[type_id] = deltas;
				}

				if (!old_type_id.TryGetValue(delta_stream_id, out rv)) {
					old_type_id[delta_stream_id] = rv = new message_wrapper(message_factory.from_type_id(type_id));
				}

				rv.msg.read_in_delta_mode = read_in_delta_mode;

				if (rv.non_delta_seen == false && read_in_delta_mode == false)
					rv.non_delta_seen = true;

				rv.msg.Read(proto);
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
		publish(string topic_name, message_base msg, bool send_as_delta = false, int delta_stream_id = 1, long message_sequence_number = 0, long micros_since_epoch = 0) 
		{
			if (delta_stream_id < 1)
				throw new ArgumentException("delta_stream_id must be greater than 0");

			stream = new MemoryStream();
			var proto = new TCompactProtocol(new TStreamTransport(stream, stream));

			if (send_as_delta == false) {
				msg.set_modified_flag();
				delta_stream_id = -delta_stream_id;
			}
			proto.WriteI32(delta_stream_id);
			proto.WriteI32((int)msg.get_type_id());
			msg.Write(proto);

			IModel channel;
			if (!publishing_channels.TryGetValue(topic_name, out channel)) {
				channel = conn.CreateModel();
				if (props == null)
					props = channel.CreateBasicProperties();
				publishing_channels[topic_name] = channel;
			}


			// publishing_channel.QueueDeclare(topic_name, false, false, false, null);
			// not too fast at the moment (bare bones example), TODO later will set topic_name elsewhere so as not to resend it everytime

			// explicit since epoch in microseconds time for the server's requirements
			if (micros_since_epoch != 0 && message_sequence_number != 0) {
				var dictionary = new Dictionary<string, object>();
				dictionary.Add("XXXXXX", message_sequence_number);
				props.ContentType = "X";
				props.Headers = dictionary;
				props.Timestamp = new AmqpTimestamp(micros_since_epoch);
				channel.BasicPublish("", topic_name, props, stream.ToArray());
			} else 
				channel.BasicPublish("", topic_name, null, stream.ToArray());

			msg.clear_modified_flag();

            //Console.WriteLine("thrift size: " + stream.Length);
           //Console.WriteLine("thrift size: " + stream.ToArray().Length.ToString());
		}

	public long
		timestamp_now()
		{
			var since_epoch = DateTime.UtcNow - epoch;
			return (long)since_epoch.TotalMilliseconds * 1000L;
		}
}
}
