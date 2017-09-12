using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Net.Security;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using Thrift;
using Thrift.Protocol;
using Thrift.Transport;
using RabbitMQ.Client;
using RabbitMQ.Client.Events;
using RabbitMQ.Util;
using RabbitMQ.Client.MessagePatterns;

namespace data_processors {

public interface inotify<V> {
	void on_new(V v);
	void on_add(V v);
	void on_modify(V v);
	void on_delete(V v);
}

public interface inotify<K, V> {
	void on_new(K k, V v);
	void on_add(K k, V v);
	void on_modify(K k, V v);
	void on_delete(K k, V v);
}

public abstract class message_base {
	public object user_data = null;
	public bool modified_flag = true;
	public bool skip_on_read_flag = false;
	public bool read_in_delta_mode = false;
  public virtual void Read<T>(TProtocol iprot, data_processors.message_envelope<T> envelope) where T : data_processors.message_base, new()
	{
		this.Read(iprot);
	}
	public virtual void Read(TProtocol tProtocol){}
	abstract public void Read_from_wire_to_custom_json(TProtocol iprot, TProtocol oprot);
	public virtual void Write(TProtocol tProtocol, bool inhibit_delta_mode_already_set_in_parent = false){}
	abstract public void write_custom_json_representation(TProtocol oprot);
	public virtual void inhibit_delta() {}
}

[Serializable]
public abstract class typed_tbase : data_processors.message_base, TBase
{
	public static data_processors.byte_array_comparator byte_comparator = new data_processors.byte_array_comparator();
	public typed_tbase parent = null;
	public bool inhibit_delta_mode = false;
	abstract public void Write(TProtocol oprot);
	public virtual void propagate_modified_flag_upstream() {
		if (modified_flag == false) {
			modified_flag = true;
			if (parent != null)
				parent.propagate_modified_flag_upstream();
		}
	}
	public static bool Symbolic_enums = false;
}

public interface message_factory_base {
	message_base from_type_name(string name, string routing_key);
}

public class amqp_envelope {
	public string routing_key;
	public IBasicProperties properties;
	public ulong
	decode_timestamp()
	{
		return (ulong)properties.Timestamp.UnixTime;
	}
	public ulong
	decode_sequence_number()
	{
		return (ulong)(long)properties.Headers["XXXXXX"];
	}
};

public class message_envelope<WaypointsType> where WaypointsType : message_base, new() {
	public enum parsing_status_enum : uint {begin, in_progress, end};
	public uint parsing_status = (uint)parsing_status_enum.begin;
	public Queue<Tuple<ulong, Action>> pending_decoders = new Queue<Tuple<ulong, Action>>();

	// really to just save on extra locking of 'queue' in between job-starting thread and Tasks...
	public Action decoder; 

	public enum flags_enum : uint {has_waypoints = 0x1, is_delta = 0x2, has_stream_id = 0x8};
	public uint flags;
	public WaypointsType waypoints = null;
	public int wire_size;
	public message_base msg;
	public bool non_delta_seen = false;
	public string type_name;
	public amqp_envelope amqp;
	public byte[] Raw_data_on_wire = null;
	public volatile bool Received_flag = false;
	public message_envelope(message_base msg_, consumer_queue<WaypointsType> Consumer, TimeSpan? Idle_timeout) {
		msg = msg_;
		if (Idle_timeout != null) {
			Task.Run(async () => Idle_sensor(Consumer, Idle_timeout.Value));
		}
	}

	public async Task Idle_sensor(consumer_queue<WaypointsType> Consumer, TimeSpan Idle_timeout) {
		var Async_delay = Task.Delay(Idle_timeout);
		await Async_delay;
		try {
			if (Async_delay.Status != TaskStatus.RanToCompletion) 
				throw new Exception("await Task.Delay for Idle_timeout");
			if (Consumer.stopped == false) {
				if (Received_flag == false) {
					Consumer.Forget_topic(amqp.routing_key); // Should (eventually) take itself out from dict of accumulated messages.
					return;
				} 
				Received_flag = false;
				Task.Run(async () => Idle_sensor(Consumer, Idle_timeout));
			}
		} catch (Exception e) {
			lock (Consumer.queue) {
				Consumer.AddToExceptionText(e.Message);
			}
		}
	}

	public bool Has_waypoints()
	{
		return ((flags & (uint)flags_enum.has_waypoints) != 0) ? true : false;
	}

};


internal static class native_methods {
	internal static class native_methods_impl {
		[DllImport("msvcrt.dll", CallingConvention=CallingConvention.Cdecl)] 
		public static extern unsafe int memcmp(byte * b1, byte * b2, long count); 
	}
	public static unsafe int 
	memcmp(byte[] b1, byte[] b2, long count)
	{
		fixed (byte * p1 = b1, p2 = b2 ) {
			return native_methods_impl.memcmp(p1, p2, count);
		}
	}
}

//TODO -- see if can just assert that no lhs/rhs shall be null
public class byte_array_comparator : IEqualityComparer<byte[]>, IComparer<byte[]> {
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public bool
	Equals(byte[] lhs, byte[] rhs)
	{
		bool nothing_in_lhs = (lhs == null || lhs.Length == 0);
		bool nothing_in_rhs = (rhs == null || rhs.Length == 0);
		if (nothing_in_lhs == true || nothing_in_rhs == true)
			return nothing_in_lhs == nothing_in_rhs;
		else
			return lhs.SequenceEqual(rhs);
	}
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public int
	GetHashCode(byte[] x)
	{
		uint hash = 2166136261u;
		foreach (var i in x)
			hash = (hash ^ (uint)i) * 16777619u;
		hash += hash << 13;
		hash ^= hash >> 7;
		hash += hash << 3;
		hash ^= hash >> 17;
		hash += hash << 5;
		return (int)hash;
	}
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public int
	Compare(byte[] lhs, byte[] rhs)
	{
		bool nothing_in_lhs = (lhs == null || lhs.Length == 0);
		bool nothing_in_rhs = (rhs == null || rhs.Length == 0);
		if (nothing_in_lhs == true &&  nothing_in_rhs == true)
			return 0;
		else if (nothing_in_lhs)
			return -1;
		else if (nothing_in_rhs)
			return 1;
		else if (lhs.Length < rhs.Length)
			return -1;
		else if (lhs.Length > rhs.Length)
			return 1;
		else
			return native_methods.memcmp(lhs, rhs, lhs.Length);
	}
}

public class consumer_queue<WaypointsType> : DefaultBasicConsumer where WaypointsType : message_base, new() 
{
	public Queue<message_envelope<WaypointsType>> queue = new Queue<message_envelope<WaypointsType>>();
	ulong messages_size = 0;

	message_factory_base message_factory;
	public ConcurrentDictionary<string, ConcurrentDictionary<string, ConcurrentDictionary<byte[], message_envelope<WaypointsType>>>> previous_messages = new ConcurrentDictionary<string, ConcurrentDictionary<string, ConcurrentDictionary<byte[], message_envelope<WaypointsType>>>>();

	bool Save_raw_data_on_wire;
	TimeSpan? Idle_timeout;

	public consumer_queue(ReaderWriterLockSlim rwl, message_factory_base message_factory_, IModel model, bool Save_raw_data_on_wire_, TimeSpan? Idle_timeout) 
	: base(model) { 
		this.rwl = rwl;
		message_factory = message_factory_;
		this.Save_raw_data_on_wire = Save_raw_data_on_wire_;
		this.Idle_timeout = Idle_timeout;

		// Todo: move to user/app/client level code!
		//var Maximum_concurrency = Environment.ProcessorCount * 2;
		//ThreadPool.SetMaxThreads(Maximum_concurrency, Maximum_concurrency);
	}

	public volatile bool stopped = false;
	uint cached_messages_max_size = 5;
	ReaderWriterLockSlim rwl = null;

	public void AddToExceptionText(string MoreText) {
		Exception_text = (Exception_text == null ? "" : Exception_text) + Environment.NewLine + MoreText;
	}

	void AddToLogText(string MoreText) {
		Log_text = (Log_text == null ? "" : Log_text) + Environment.NewLine + MoreText;
	}

	// Note: queue should already be locked.
	void Start_parsing_job(message_envelope<WaypointsType> rv) {
		var decoder_tuple = rv.pending_decoders.Peek();
		rv.decoder = decoder_tuple.Item2;
		Task.Factory.StartNew(() => {
			if (stopped == true)
				return;
			string Error = null;
			rwl.EnterReadLock();
			try {
				rv.decoder();
			} catch (Exception e) {
				Error = e.Message;
			} finally {
				rwl.ExitReadLock();
				lock(queue) {
					Debug.Assert(rv.parsing_status == (uint)message_envelope<WaypointsType>.parsing_status_enum.in_progress);
					if (Error == null) {
						Debug.Assert(rv.pending_decoders.Count != 0);
						rv.pending_decoders.Dequeue();
						rv.parsing_status = (uint)message_envelope<WaypointsType>.parsing_status_enum.end;
					} else  
						AddToExceptionText(Error);
					Monitor.PulseAll(queue);
				}
			}
		});
	}

	//~~~ expects 'queue' to be locked already!
	public void
	release(message_envelope<WaypointsType> rv) 
	{
		Debug.Assert(rv != null);
		Debug.Assert(rv.parsing_status == (uint)message_envelope<WaypointsType>.parsing_status_enum.end);
		if (rv.pending_decoders.Count != 0) {
			Debug.Assert(queue.Count != 0);
			rv.parsing_status = (uint)message_envelope<WaypointsType>.parsing_status_enum.in_progress;
			Start_parsing_job(rv);
		} else
			rv.parsing_status = (uint)message_envelope<WaypointsType>.parsing_status_enum.begin;
	}//```

	//~~~ expects 'queue' to be locked already!
	public message_envelope<WaypointsType>
	dequeue(int timeout_ms = Timeout.Infinite)
	{
		//Console.WriteLine("dequeueing...");
		while (true) {
			if (queue.Count != 0) {
				var rv = queue.Peek();
				if (rv.parsing_status == (uint)message_envelope<WaypointsType>.parsing_status_enum.end) {
					//Console.WriteLine("done dequeueing.");
					return queue.Dequeue();
				}
			} 
			if (stopped == true || Monitor.Wait(queue, timeout_ms) == false)
				return null;
		}
	}//```

	public void Forget_topic(string Topic_name) {
		//Console.WriteLine("Unbinding " + Topic_name);
		lock (queue)
			AddToLogText("Unbinding " + Topic_name);
		Model.QueueUnbind(Topic_name, "", Topic_name, null);
		((IDictionary<string, ConcurrentDictionary<string, ConcurrentDictionary<byte[], message_envelope<WaypointsType>>>>)previous_messages).Remove(Topic_name);
	}

	public string Exception_text = null;
	public string Log_text = null;
	public override void HandleBasicDeliver(string consumerTag,
			ulong deliveryTag,
			bool redelivered,
			string exchange,
			string routingKey,
			IBasicProperties props,
			byte[] body)
	{
		try {
			if (body == null || body.Length == 0)
				return;

			//Console.WriteLine("new msg from amqp");

			MemoryStream stream = new MemoryStream(body);
			var proto = new TCompactProtocol(new TStreamTransport(stream, stream));

			sbyte version = proto.ReadByte();
			if (version != 1)
				throw new ArgumentException("received unsupported version number");

			uint flags = (uint)proto.ReadI32();
			string type_name = proto.ReadString();

			byte[] stream_id = null;
			if ((flags & (uint)message_envelope<WaypointsType>.flags_enum.has_stream_id) != 0) 
				stream_id = proto.ReadBinary();
			else 
				stream_id = new byte[0];
			Debug.Assert(stream_id != null);

			var old_subject = previous_messages.GetOrAdd(routingKey, (_) => { return new ConcurrentDictionary<string, ConcurrentDictionary<byte[], message_envelope<WaypointsType>>>();}); 
			var old_type_name = old_subject.GetOrAdd(type_name, (_) => {return new ConcurrentDictionary<byte[], message_envelope<WaypointsType>>(new byte_array_comparator());}); 
			var rv = old_type_name.GetOrAdd(stream_id, (_) => { return new message_envelope<WaypointsType>(message_factory.from_type_name(type_name, routingKey), this, Idle_timeout); });

			rv.Received_flag = true;

			lock (queue) {
				Action message_parser = delegate(){ //~~~ message-reading lambda
					unchecked {
						if (Save_raw_data_on_wire == true)
							rv.Raw_data_on_wire = body;
						rv.type_name = type_name;
						rv.amqp = new amqp_envelope { routing_key = routingKey, properties = props };
						rv.wire_size = body.Length;
						rv.flags = flags;
						if (rv.Has_waypoints() == true) {
							if (rv.waypoints == null)
								rv.waypoints = new WaypointsType();
							rv.waypoints.read_in_delta_mode = false;
							rv.waypoints.Read(proto);
						}
						if ((rv.flags & (uint)message_envelope<WaypointsType>.flags_enum.is_delta) != 0) {
							rv.msg.read_in_delta_mode = true;
						} else {
							if (rv.non_delta_seen == false)
								rv.non_delta_seen = true;
							rv.msg.read_in_delta_mode = false;
						}

						rv.msg.Read(proto, rv);
						// for testing only
						//GC.Collect();
						//GC.WaitForPendingFinalizers();

						//Console.WriteLine("read...");
					}
				};//```

				while (stopped == false && rv.pending_decoders.Count > cached_messages_max_size)
					Monitor.Wait(queue);
				if (stopped == true)
					return;

				rv.pending_decoders.Enqueue(Tuple.Create<ulong, Action>(++messages_size, message_parser));
				queue.Enqueue(rv);

				if (rv.parsing_status == (uint)message_envelope<WaypointsType>.parsing_status_enum.begin && rv.pending_decoders.Count == 1) {
					rv.parsing_status = (uint)message_envelope<WaypointsType>.parsing_status_enum.in_progress;
					Start_parsing_job(rv);
				}
			}
		} catch (Exception e) {
			lock(queue)
				AddToExceptionText(e.Message);
			stop();
		}
	}

	public override void HandleModelShutdown(object Model, ShutdownEventArgs Reason) {
		lock(queue)
			AddToExceptionText(Reason.ToString());
		base.HandleModelShutdown(Model, Reason);
	}

	public void
	stop()
	{
		stopped = true;
		lock (queue)
			Monitor.PulseAll(queue);
	}

	public override void OnCancel()
	{
		stop();
		base.OnCancel();
	}
}

public class synapse_client<WaypointsType> where WaypointsType : message_base, new() {
	ConnectionFactory factory;
	IConnection conn;
	IModel ch = null; // subscribing
	TimeSpan? Publishing_channel_idle_timeout = null;
	public class Publishing_exception_type {
		public string Exception_text = null; 
		public void AddToExceptionText(string MoreText) {
			Exception_text = (Exception_text == null ? "" : Exception_text) + Environment.NewLine + MoreText;
		}
	}
	public class Publishing_channel {
		public volatile bool Published_flag = false;
		public IModel AMQP_channel = null;

		public async Task Idle_sensor(TimeSpan Idle_timeout, string Topic_name, Dictionary<string, Publishing_channel> publishing_channels, Publishing_exception_type e) {
			var Async_delay = Task.Delay(Idle_timeout);
			await Async_delay;
			try {
				if (Async_delay.Status != TaskStatus.RanToCompletion) 
					throw new Exception("await Task.Delay for Idle_timeout");
				if (Published_flag == false) {
					lock (publishing_channels) {
						if (Published_flag == false) {
							AMQP_channel.Close();
							publishing_channels.Remove(Topic_name); // Takes itself out!
							return;
						}
					}
				} 
				Published_flag = false;
				Task.Run(async () => Idle_sensor(Idle_timeout, Topic_name, publishing_channels, e));
			} catch (Exception exception) {
				lock (e) {
					e.AddToExceptionText(exception.Message);
				}
			}
		}

		public Publishing_channel(IConnection Connection, TimeSpan? Idle_timeout, string Topic_name, Dictionary<string, Publishing_channel> publishing_channels, Publishing_exception_type e) {
			AMQP_channel = Connection.CreateModel();
			if (Idle_timeout != null)
				Task.Run(async () => Idle_sensor(Idle_timeout.Value, Topic_name, publishing_channels, e));
		}
	}
	Dictionary<string, Publishing_channel> publishing_channels = new Dictionary<string, Publishing_channel>(); // publishing (class is a bit of a duplex(ish) client)
	public Publishing_exception_type publishing_exception = new Publishing_exception_type();
	consumer_queue<WaypointsType> consumer = null;

	IBasicProperties props = null;

	public synapse_client(SslPolicyErrors? use_ssl = null, string server_host = "localhost", int server_port = 5672, bool tcp_no_delay = false, int so_rcvbuf = -1, int so_sndbuf = -1, ushort heartbeat = 0, int socket_rw_timeout = 30000, TimeSpan? Publishing_topic_timeout = null) 
	{
		Publishing_channel_idle_timeout = Publishing_topic_timeout;
		// AMQP connection
		factory = new ConnectionFactory();
		factory.RequestedHeartbeat = heartbeat;
		factory.HostName = server_host;
		factory.Port = server_port;
		if (use_ssl != null) {
			factory.Ssl.Enabled = true;
			factory.Ssl.ServerName = server_host;
			// factory.Ssl.RemoteCertificateValidationCallback 
			factory.Ssl.AcceptablePolicyErrors = use_ssl.Value;
		}
		factory.SocketFactory = delegate(AddressFamily address_family) {
			var rv = new TcpClient(address_family) { NoDelay = tcp_no_delay };
			if (so_rcvbuf != -1)
				rv.ReceiveBufferSize = so_rcvbuf;
			if (so_sndbuf != -1)
				rv.SendBufferSize = so_sndbuf;
			return rv;
		};
		conn = factory.CreateConnection();
		conn.socket_timeout(socket_rw_timeout);
		ch = conn.CreateModel();
		conn.AutoClose = true;
	}

	public void
	close()
	{
		if (consumer != null) {
			consumer.stop();
			consumer = null;
		}
		// ... done
		lock (publishing_channels) {
			foreach (var pub_i in publishing_channels)
				pub_i.Value.AMQP_channel.Close();
			publishing_channels.Clear();
		}
		if (ch != null) {
			ch.Close();
			ch = null;
		}
	}

	~synapse_client()
	{
		close();
	}

	public void
	subscribe(ReaderWriterLockSlim rwl, message_factory_base message_factory, string topic_name, ulong begin_utc = 0, ulong end_utc = 0, ulong delayed_delivery_tolerance = 0, bool supply_metadata = false, bool tcp_no_delay = false, bool preroll_if_begin_timestamp_in_future = false, bool Save_raw_data_on_wire = false, TimeSpan? Idle_timeout = null)
	{
		if (consumer == null)
			consumer = new consumer_queue<WaypointsType>(rwl, message_factory, ch, Save_raw_data_on_wire, Idle_timeout);
		var dictionary = new Dictionary<string, object>();

		// currently 'l' in RabbitMQ is chosen as signed type (not the same in other language AMQP libs)... so our interface/API/and-server-side is uint64_t and then each lib/language/implementation casts as necessary (timestamping is under 63 bits at the moment at the server side anyways)

		if (begin_utc != 0)
			dictionary.Add("begin_timestamp", (long)begin_utc);

		if (end_utc != 0)
			dictionary.Add("end_timestamp", (long)end_utc);

		if (delayed_delivery_tolerance != 0)
			dictionary.Add("delayed_delivery_tolerance", (long)delayed_delivery_tolerance);

		if (supply_metadata == true)
			dictionary.Add("supply_metadata", (int)1);

		if (tcp_no_delay == true)
			dictionary.Add("tcp_no_delay", (int)1);

		if (preroll_if_begin_timestamp_in_future == true)
			dictionary.Add("preroll_if_begin_timestamp_in_future", (int)1);

		// not too fast at the moment (bare bones example), TODO later will set topic_name elsewhere so as not to resend it everytime
		ch.BasicConsume(topic_name, true, "", dictionary, consumer);
	}

	public void Forget_subscribed_topic(string Topic_name) {
		consumer.Forget_topic(Topic_name);
	}

	public void Forget_published_topic(string Topic_name) {
		publishing_channels[Topic_name].AMQP_channel.Close();
		publishing_channels.Remove(Topic_name);
	}

	// TODO these are temporary wrappers (will likely be deprecated in favour of synapse_client either becoming more channel-capable, or refactored into synapse_consumer and synapse_publisher types of entities).
	public ConcurrentDictionary<string, ConcurrentDictionary<string, ConcurrentDictionary<byte[], message_envelope<WaypointsType>>>> previous_messages { get {return consumer.previous_messages; } }
	public message_envelope<WaypointsType> last_rv = null;
	public string Log_text = null;
	public message_envelope<WaypointsType>
	next(int timeout_ms = Timeout.Infinite)
	{
		lock (consumer.queue) {
			Log_text = consumer.Log_text;
			consumer.Log_text = null;
			if (last_rv != null)
				consumer.release(last_rv);
			last_rv = consumer.dequeue(timeout_ms);
			// Todo -- later more elegance.
			if (consumer.Exception_text != null)
				throw new Exception(consumer.Exception_text);
			else if (publishing_exception.Exception_text != null) {
				lock(publishing_exception)
					throw new Exception(publishing_exception.Exception_text);
			}
			return last_rv;
		}
	}
	public void
	release()
	{
		if (last_rv != null) {
			lock (consumer.queue) {
				consumer.release(last_rv);
				last_rv =  null;
			}
		}
	}

	public void
	ensure_publishing_channel(string topic_name)
	{
		if (publishing_channels.ContainsKey(topic_name) == false) {
			var channel = new Publishing_channel(conn, Publishing_channel_idle_timeout, topic_name, publishing_channels, publishing_exception);
			if (props == null)
				props = channel.AMQP_channel.CreateBasicProperties();
			publishing_channels[topic_name] = channel;
		}
	}

	public long
	publish(string topic_name, message_base msg, WaypointsType waypoints = null, bool send_as_delta = false, byte[] stream_id = null, long message_sequence_number = 0, ulong micros_since_epoch = 0) 
	{
		unchecked {
			MemoryStream stream = new MemoryStream();
			var proto = new TCompactProtocol(new TStreamTransport(stream, stream));

			proto.WriteByte(1);

			uint flags = 0;
			if (waypoints != null)
				flags |= (uint)message_envelope<WaypointsType>.flags_enum.has_waypoints;
			if (send_as_delta == true)
				flags |= (uint)message_envelope<WaypointsType>.flags_enum.is_delta;
			if (stream_id != null)
				flags |= (uint)message_envelope<WaypointsType>.flags_enum.has_stream_id;
			proto.WriteI32((int)flags);

			proto.WriteString(msg.GetType().Name);

			if (stream_id != null)
				proto.WriteBinary(stream_id);

			if (waypoints != null)
				waypoints.Write(proto);

			msg.Write(proto, !send_as_delta);

			Publishing_channel channel;			
			string topic_name_;
			lock (publishing_channels) {
				if (!publishing_channels.TryGetValue(topic_name, out channel)) {
					channel = new Publishing_channel(conn, Publishing_channel_idle_timeout, topic_name, publishing_channels, publishing_exception);
					publishing_channels[topic_name] = channel;
					topic_name_ = topic_name;
				} else {
					topic_name_ = "";
				}

				// publishing_channel.QueueDeclare(topic_name, false, false, false, null);
				// not too fast at the moment (bare bones example), TODO later will set topic_name elsewhere so as not to resend it everytime

				// explicit since epoch in microseconds time for the server's requirements
				// TODO -- perhaps drop seq. number altogether or provide individually-parsable (at the server end) possibility of supplying either one, or both (currently it is both or none thereby wasting at times 8 bytes)...
				IBasicProperties props_;
				if (micros_since_epoch != 0 || message_sequence_number != 0) {
					var dictionary = new Dictionary<string, object>();
					dictionary.Add("XXXXXX", (long)message_sequence_number);
					if (props == null)
						props = channel.AMQP_channel.CreateBasicProperties();
					props.ContentType = "X";
					props.Headers = dictionary;
					props.Timestamp = new AmqpTimestamp((long)micros_since_epoch);
					props_ = props;
				} else
					props_ = null;
				channel.Published_flag = true;
				channel.AMQP_channel.BasicPublish("", topic_name_, props_, stream.ToArray());
			}
			return stream.Length;
		}
	}

}
public class synapse_client_utils {
	public static DateTime epoch = new DateTime(1970, 1, 1);
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public static ulong
	timestamp_now()
	{
		return timestamp(DateTime.UtcNow);
	}
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public static ulong
	timestamp(DateTime x)
	{
		var since_epoch = x - epoch;
		return (ulong)since_epoch.Ticks / 10;
	}
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public static DateTime
	timestamp_to_datetime(ulong timestamp) {
		return epoch.AddTicks((long)(timestamp * 10));
	}
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	public static ulong
	timespan(TimeSpan x) {
		return (ulong)x.Ticks / 10;
	}
}
}


