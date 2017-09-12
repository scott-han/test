using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Collections.Generic;
using contests4;

namespace ConsoleApplication1
{
	class Program
	{
		public static void Main(string[] args)
		{
			try
			{
				// create client
				var client = new data_processors.synapse_client(new type_factory());

				var bet_pool = new ImaginaryBetPool();

				Random rnd = new Random();
				// Custom message creation
				for (int i = 0; i != 30; ++i) {
					if ((i % 2) == 0) {
						if (bet_pool.get_weather_records_element(3) == null)
							bet_pool.set_weather_records_element(3, new WeatherRecordType());
						bet_pool.get_weather_records_element(3).set_xid("there it is");
					} else {
						if ((i % 3) == 0)
							bet_pool.set_weather_records_element(3, null);
						else
							bet_pool.get_weather_records_element(3).set_xid("modified xid...");
					}

					bet_pool.set_xid("11-06-2008;Hong+Kong;1;1208%3AHK;quartet;1;4.1;150;50;b");
					bet_pool.set_sid("optionally set sid value:x12:1251:777");
					if (bet_pool.get_description() == null)
						bet_pool.set_description(new TextTypeExtended());
					bet_pool.get_description().set_description("sample description of a message for the betting pool(s)");

					// build some sample combos 
					for (byte a = 1; a != 5; ++a) {
						for (byte b = 1; b != 5; ++b) {
							for (byte c = 1; c != 5; ++c) {
								for (byte d = 1; d != 5; ++d) {
									byte[] key = new byte[]{a,b,c,d};
									if (rnd.NextDouble() < .1) // alternate addition and deletion of keys -- for testing illustration purposes...
										bet_pool.set_combinations_element(key, i + rnd.NextDouble() * .777);
									else
										bet_pool.set_combinations_element(key, null);
								}
							}
						}
					}
					var timestamp = bet_pool.get_When();
					if (timestamp == null) {
						timestamp = new TimestampType();				
						bet_pool.set_When(timestamp);
					}
					timestamp.set_Value(client.timestamp_now());

					// send it out ...
					client.publish("test.thrift.hong_kong.leon", bet_pool, i % 10 == 0 ? false : true); // ... using inter-message delta serialistaion, with every 10th message being sent as a whole.
					//client.publish("test.thrift.hong_kong.leon", bet_pool, false);
					Console.WriteLine("\npublished '{0}'", bet_pool);
					var sorted_dict = new SortedDictionary<byte[],double>(bet_pool.get_combinations(), new byte_array_comparator());
					foreach(var ii in sorted_dict) {
						string text_key = " ";
						foreach(var k in ii.Key)
							text_key += k.ToString() + ' ' ;
						Console.WriteLine("betpool combination: ['{0}'] '{1}'", text_key, ii.Value);
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
