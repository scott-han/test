using System;
using System.Data;
using System.Diagnostics;
using System.IO;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Net;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using System.Runtime;
using Thrift;
using Thrift.Protocol;
using Thrift.Transport;
using data_processors;
using data_processors.federated_serialisation;

// NOTE -- not really for testing CPU-bound, on-localhost, connections. The main limiting factor expected to be determined by this app is the WAN(ish) network-characteristiscs... so if/when CPU/local-sys resources become saturated then this app begin to run outside its designated context.


namespace roundtrip_tester {

public class snd_ping {
	public ulong published_timestamp;
	public DateTime local_timestamp;
	public ulong msg_size;
}

public class synapse_publisher {
	synapse_client<DataProcessors.Waypoints.waypoints> client = null;
	DataProcessors.Contests4.Contests msg = null;
	Thread worker = null;
	public bool error = false;
	public Queue<snd_ping> stats = new Queue<snd_ping>();
	public void run(SslPolicyErrors? use_ssl, string synapse_host, int synapse_port, string topic_name, string seed_from_topic_name, bool tcp_no_delay, int so_rcvbuf, int so_sndbuf)
	{
		client = new synapse_client<DataProcessors.Waypoints.waypoints>(use_ssl, synapse_host, synapse_port, tcp_no_delay, so_rcvbuf, so_sndbuf, 30, 100000);
		client.subscribe(new ReaderWriterLockSlim(), new DataProcessors.Contests4.type_factory(), seed_from_topic_name, 1);
		for (var msg_wrapper = client.next(5000); msg_wrapper != null; msg_wrapper = client.next(5000))
			if (msg_wrapper.type_name == "Contests" && msg_wrapper.non_delta_seen == true) {
				msg = new DataProcessors.Contests4.Contests();
				msg.set_from((DataProcessors.Contests4.Contests)msg_wrapper.msg);
				msg.set_dataprovider("here comes some unicode \u76F4 and back to ascii :)");
				Console.Error.WriteLine("seeded message size " + msg_wrapper.wire_size);
				break;
			}
		client.close();
		client = null;
		if (msg == null)
			throw new System.Exception("could not seed");
		worker = new Thread(new ThreadStart(() => {
			Console.Error.WriteLine("synapse_publisher running...");
			try
			{
				client = new data_processors.synapse_client<DataProcessors.Waypoints.waypoints>(use_ssl, synapse_host, synapse_port, tcp_no_delay, so_rcvbuf, so_sndbuf, 30, 100000);
				for (uint i = 1; i != 10001 + 1; ++i) {
					var p = new snd_ping();
					p.published_timestamp = i;
					p.local_timestamp = DateTime.UtcNow;
					p.msg_size = (ulong)client.publish(topic_name, msg, null, false, null, 0, i);
					stats.Enqueue(p);
				}
			} catch { 
				error = true;
				Console.Error.WriteLine("synapse_publisher exception");
			} finally {
				if (client != null)
					client.close();
			}
		}));
		worker.Start();
	}

	public void wait()
	{
		if (worker != null)
			worker.Join();
	}
}

public class rcv_ping {
	public ulong published_timestamp;
	public DateTime local_timestamp;
}

public class synapse_subscriber {
	Thread worker = null;
	public bool error = false;
	public Queue<rcv_ping> stats = new Queue<rcv_ping>();
	public void run(SslPolicyErrors? use_ssl, string synapse_host, int synapse_port, string topic_name, bool tcp_no_delay, int so_rcvbuf, int so_sndbuf)
	{
		worker = new Thread(new ThreadStart(() => {
			Console.Error.WriteLine("synapse_client running...");
			data_processors.synapse_client<DataProcessors.Waypoints.waypoints> client = null;
			try
			{
				client = new data_processors.synapse_client<DataProcessors.Waypoints.waypoints>(use_ssl, synapse_host, synapse_port, tcp_no_delay, so_rcvbuf, so_sndbuf, 30, 100000);
				client.subscribe(new ReaderWriterLockSlim(), new DataProcessors.Contests4.type_factory(), topic_name, 
					1, 
					0, // until 
					data_processors.synapse_client_utils.timespan(new TimeSpan(20, 0, 0)), // delayed delivery tolerance 
					true, tcp_no_delay);

				for (var msg_wrapper = client.next(7000); msg_wrapper != null; msg_wrapper = client.next(7000)) {
					var p = new rcv_ping();
					p.published_timestamp = msg_wrapper.amqp.decode_timestamp();
					p.local_timestamp = DateTime.UtcNow;
					stats.Enqueue(p);
					if (p.published_timestamp % 100 == 0)
						Console.WriteLine("sub got message: " + p.published_timestamp);
				}
			} catch { 
				error = true;
				Console.Error.WriteLine("synapse_subscriber exception");
			} finally {
				if (client != null)
					client.close();
			}
		}));
		worker.Start();
	}

	public void wait()
	{
		if (worker != null)
			worker.Join();
	}
}


class Program {
	static void Main(string[] args)
	{
		GCSettings.LargeObjectHeapCompactionMode = GCLargeObjectHeapCompactionMode.CompactOnce;
		GCSettings.LatencyMode = GCLatencyMode.LowLatency;
		bool use_ssl = false;
		string synapse_host = null;
		int synapse_port = 0;
		bool tcp_no_delay = false;
		int so_rcvbuf = -1;
		int so_sndbuf = -1;
		string topic_name = null;
		for (int i = 0; i != args.Length; ++i) {
			if (i == args.Length - 1)
				throw new System.Exception("incorrect args, " + args[i] + " ought to have an explicit value");
			else if (args[i] == "--use_ssl")
				use_ssl = Convert.ToBoolean(args[++i]);
			else if (args[i] == "--synapse_at") {
				var host_port = args[++i].Split(':');
				synapse_host = host_port[0];
				if (host_port.Length > 1)
					synapse_port = Convert.ToInt32(host_port[1]);
			} else if (args[i] == "--tcp_no_delay")
				tcp_no_delay = Convert.ToBoolean(args[++i]);
			else if (args[i] == "--so_rcvbuf")
				so_rcvbuf = Convert.ToInt32(args[++i]);
			else if (args[i] == "--so_sndbuf")
				so_sndbuf = Convert.ToInt32(args[++i]);
			else if (args[i] == "--topic_name")
				topic_name = "[1]." + args[++i];
			else 
				throw new System.Exception("unknown option(=" + args[i] + ')');
		}
		if (synapse_port == 0)
			synapse_port = use_ssl == true ? 5671 : 5672;
		if (synapse_host == null)
			throw new System.Exception("--synapse_at must be supplied at CLI level");
		if (topic_name == null)
			throw new System.Exception("--topic_name must be supplied at CLI level");

		var contests = new DataProcessors.Contests4.Contests();
		var contest = new DataProcessors.Contests4.Contest();
		var long_string = "a";
		for (uint x = 0; x != 17; ++x)
			long_string += long_string;
		contest.set_competition(long_string);
		contests.set_contest_element("a", contest);

		var pub = new synapse_publisher();
		var sub = new synapse_subscriber();

		// for the time-being -- later-on take out chain errors and substitue with None, currently it is here to allow self-signed certificates from synapse (until we buy some proper ones)
		pub.run(use_ssl == true ? (SslPolicyErrors?)SslPolicyErrors.RemoteCertificateChainErrors : null, synapse_host, synapse_port, topic_name, "test.betfair.contests.au", tcp_no_delay, so_rcvbuf, so_sndbuf);
		sub.run(use_ssl == true ? (SslPolicyErrors?)SslPolicyErrors.RemoteCertificateChainErrors : null, synapse_host, synapse_port, topic_name, tcp_no_delay, so_rcvbuf, so_sndbuf);
		pub.wait();
		sub.wait();
		if (pub.error || sub.error)
			throw new System.Exception("oops: something aint right...");
		Dictionary<ulong, rcv_ping> sub_stats = sub.stats.ToDictionary(x => x.published_timestamp, x => x);
		DateTime prev_pub_timestamp = pub.stats.First().local_timestamp;
		DateTime prev_sub_timestamp = sub.stats.First().local_timestamp;
		ulong accumulated_pub_size = 0;
		ulong accumulated_sub_size = 0;
		Console.WriteLine("RndTrip Latency(ms),Snd bw(Mbps),Rcv bw(Mbps),Msg size(B)");
		foreach (var pub_i in pub.stats) {
			var sub_i = sub_stats[pub_i.published_timestamp];
			
			Console.Write((sub_i.local_timestamp - pub_i.local_timestamp).TotalMilliseconds + ",");

			if (prev_pub_timestamp != pub_i.local_timestamp) {
				Console.Write((pub_i.msg_size + accumulated_pub_size) * 8 / ((pub_i.local_timestamp - prev_pub_timestamp).TotalSeconds * (1024 * 1024)));
				prev_pub_timestamp = pub_i.local_timestamp;
				accumulated_pub_size = 0;
			} else
				accumulated_pub_size = pub_i.msg_size;
			Console.Write(",");

			if (prev_sub_timestamp != sub_i.local_timestamp) {
				Console.Write((pub_i.msg_size + accumulated_sub_size) * 8 / ((sub_i.local_timestamp - prev_sub_timestamp).TotalSeconds * (1024 * 1024)));
				prev_sub_timestamp = sub_i.local_timestamp;
				accumulated_sub_size = 0;
			} else
				accumulated_sub_size = pub_i.msg_size;
			Console.Write("," + pub_i.msg_size + "\n");


		}
		Console.Error.WriteLine("bye bye");
	}
}

}
