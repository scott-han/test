using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Linq;
using contests4;

namespace ConsoleApplication1
{
	public class local_state : data_processors.user_data_interface  {
		public int i = 0;
		public int j = 1;
		public int k = 2;

		public void on_modified()
		{
			Console.WriteLine("\n\nDATA MODIFIED");
		}

		public void on_deleted()
		{
			Console.WriteLine("\n\nDATA DELETED");
		}
	}
	public class Program {
		public static void Main(string[] args)
		{
			try {
				var client = new data_processors.synapse_client(new type_factory());
				//client.subscribe("test.thrift.*.leon", new DateTime(2015, 1, 1), DateTime.UtcNow, true);
				client.subscribe("test.thrift.*.leon", new DateTime(2015, 1, 1), null, true);

				bool callbacks_attached = false;
				for (var msg_wrapper = client.next(); msg_wrapper != null; msg_wrapper = client.next()) {
					if (msg_wrapper.non_delta_seen == true) {
						var msg = (ImaginaryBetPool)msg_wrapper.msg;
						Console.WriteLine("\nreceived and parsed msg:\n'{0}', is delta: '{1}'", msg, msg.read_in_delta_mode);
						if (msg.get_type_id() != ImaginaryBetPool.type_id)
							throw new Exception("unexpected message type");
						if (callbacks_attached == false) {
							callbacks_attached = true;
							if (msg.get_weather_records() != null) {
								foreach(var j in msg.get_weather_records()) {
									j.Value.user_data = new local_state();
								}
							}
							msg.weather_records_on_add_callback = delegate(WeatherRecordType x) {
								Console.WriteLine("\n\nDATA ADDED");
								x.user_data = new local_state();
							};
						}
						if (msg.get_combinations() != null) {
							var sorted_dict = new SortedDictionary<byte[],double>(msg.get_combinations(), new byte_array_comparator());
							foreach(var i in sorted_dict) {
								string text_key = "";
								foreach(var k in i.Key)
									text_key += k.ToString() + ' ' ;
								Console.WriteLine("betpool combination: ['{0}'] '{1}'", text_key, i.Value);
							}
						}
						if (msg.get_weather_records() != null) {
							foreach(var j in msg.get_weather_records()) {
								Console.WriteLine("weather record: ['{0}'] '{1}'", j.Key, j.Value.get_xid());
								Console.WriteLine("weather record user_data: '{0}'", j.Value.user_data);
							}
						}
					}
				}

				client.close();
				Console.WriteLine("bye bye");
			} catch (Exception e) {
				Console.WriteLine("oops " + e.Message);
			}
		}
	}
}
