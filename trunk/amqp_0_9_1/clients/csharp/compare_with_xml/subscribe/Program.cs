using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Collections.Generic;
using data_processors;
using System.Xml.Serialization;


namespace ConsoleApplication1
{
	public class c 
	{
        [XmlAttribute]
		public int r;
        [XmlAttribute]
		public double p;
	}

	public class imaginary_bet_pool_xml : data_processors.xml.message_base
	{
		public const uint type_id = 0;
		public override uint get_type_id() { return type_id;}
		public long When;
		public string description = null;
		public string xid = null;
		public string sid = null;
		public List<c> combinations;
	}

	class Program
	{
		public static void Main(string[] args)
		{
			try {
				var iterate_over = Convert.ToInt32(args[1]);
				System.Diagnostics.Stopwatch sw = null;

				{
					var client = new synapse_client(new type_factory(), args[0]);
					client.subscribe("test.compare_xml_with_thrift_a.leon", new DateTime(1970, 1, 2), null, true);

					var i = -1;
					sw = System.Diagnostics.Stopwatch.StartNew();
					for (var msg_wrapper = client.next(); msg_wrapper != null && ++i != iterate_over; msg_wrapper = client.next()) {
						if (msg_wrapper.msg.get_type_id() != imaginary_bet_pool.type_id)
							throw new Exception("unexpected message");
					}                    
					sw.Stop();
					Console.WriteLine("thrift done. each message processed in: " + sw.ElapsedMilliseconds / (double)iterate_over + "(ms)");
					client.close();
				}

				{
					var client = new data_processors.xml.synapse_client(new data_processors.xml.type_factory(), args[0]);
					client.subscribe("test.compare_xml_with_thrift_b.leon", new DateTime(1970, 1, 2), null, true);

					sw = System.Diagnostics.Stopwatch.StartNew();
					var i = -1;
					for (var msg_wrapper = client.next(); msg_wrapper != null && ++i != iterate_over; msg_wrapper = client.next()) {
						if (msg_wrapper.msg.get_type_id() != imaginary_bet_pool_xml.type_id)
							throw new Exception("unexpected message");
					}
					sw.Stop();
					Console.WriteLine("xml done. each message processed in: " + sw.ElapsedMilliseconds / (double)iterate_over + "(ms)");
					client.close();
				}

				Console.WriteLine("bye bye");
			} catch (Exception e) {
				Console.WriteLine("oops " + e.Message);
			}
		}
	}
}
