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
			try
            {
                var iterate_over = Convert.ToInt32(args[2]);
                var total_runners = Convert.ToInt32(args[1]);
                ++total_runners;
                System.Diagnostics.Stopwatch sw = null;
                Random rnd = null;
                



                {
                    rnd = new Random();
                    var client = new synapse_client(new type_factory(), args[0]);

                    // Custom message creation
                    sw = System.Diagnostics.Stopwatch.StartNew();
                    for (int i = 0; i != iterate_over; ++i)
                    {
                        var bet_pool = new imaginary_bet_pool();
                        bet_pool.set_xid("11-06-2008;Hong+Kong;1;1208%3AHK;quartet;1;4.1;150;50;b");
                        bet_pool.set_sid("optionally set sid value:x12:1251:777");
                        if (bet_pool.get_description() == null)
                            bet_pool.set_description("sample description of a message for the betting pool(s)");

                        // build some sample combos 
                        for (byte a = 1; a != total_runners; ++a)
                        {
                            for (byte b = 1; b != total_runners - 1; ++b)
                            {
                                for (byte c = 1; c != total_runners - 2; ++c)
                                {
                                    for (byte d = 1; d != total_runners - 3; ++d)
                                    {
                                        bet_pool.set_combinations_element(new byte[] { a, b, c, d }, i * .01);
                                    }
                                }
                            }
                        }
                        bet_pool.set_When(client.timestamp_now());
                        client.publish("test.compare_xml_with_thrift_a.leon", bet_pool, false);
                    }
                    sw.Stop();
                    Console.WriteLine("thrift done. each message processed in: " + sw.ElapsedMilliseconds / (double)iterate_over + "(ms); message-rate: " + iterate_over / (sw.ElapsedMilliseconds * 0.001) + ", message size: " + client.stream.Length + ", participating runners: " + (total_runners - 1) + ", averaged over: " + iterate_over + " transactions");
                    client.close();

                }
              //  System.Threading.Thread.Sleep(10000);
                {
                    rnd = new Random();
                    var client = new data_processors.xml.synapse_client(new data_processors.xml.type_factory(), args[0]);
                    sw = System.Diagnostics.Stopwatch.StartNew();
                    for (int i = 0; i != iterate_over; ++i)
                    {
                        var bet_pool = new imaginary_bet_pool_xml();
                        bet_pool.xid = "11-06-2008;Hong+Kong;1;1208%3AHK;quartet;1;4.1;150;50;b";
                        bet_pool.sid = "optionally set sid value:x12:1251:777";
                        bet_pool.description = "sample description of a message for the betting pool(s)";
                        bet_pool.combinations = new List<c>();
                        int blag = 0;
                        // build some sample combos 
                        for (byte a = 1; a != total_runners; ++a)
                        {
                            for (byte b = 1; b != total_runners - 1; ++b)
                            {
                                for (byte c = 1; c != total_runners - 2; ++c)
                                {
                                    for (byte d = 1; d != total_runners - 3; ++d)
                                    {
                                        var combo = new c();
                                        //combo.r = a.ToString() + "," + b.ToString() + "," + c.ToString() + "," + d.ToString();
                                        combo.r = ++blag;
                                        combo.p = (double)i * .01;
                                        bet_pool.combinations.Add(combo);
                                    }
                                }
                            }
                        }
                        bet_pool.When = client.timestamp_now();
                        client.publish("test.compare_xml_with_thrift_b.leon", bet_pool);

                    }
                    sw.Stop();
                    Console.WriteLine("xml done. each message processed in: " + sw.ElapsedMilliseconds / (double)iterate_over + "(ms); message-rate: " + iterate_over / (sw.ElapsedMilliseconds * 0.001) + ", message size: " + client.stream.Length + ", participating runners: " + (total_runners - 1) + ", averaged over: " + iterate_over + " transactions");
                    client.close();
                }
                Console.WriteLine("bye bye");
            }
            catch (Exception e)
            {
				Console.WriteLine("oops " + e.Message);
			}
		}
	}
}
