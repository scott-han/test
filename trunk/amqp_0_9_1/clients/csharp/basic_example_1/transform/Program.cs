// A REASONABLY HEAVY EXAMPLE of transformation (in this case there is complete filtering-out of everything that is not relevant to WIN markets)...

// done by leveraging multi-threading decoding of different routing-keys in synapse_client.cs
// publishing is still single threaded (possible todo in future: may consider using multiple publishing instances)

// possible todo in future: an additional layer of 'close to the wire' selective cloning of buffers may be developed (may be a tid bit difficult due to the fact that some things in thrift are 'eol' delimiter-based encoded (e.g. structs) yet others are size-up-front encoded (e.g. containers). 

using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.Serialization.Formatters.Binary;
using System.Linq;
using System.Collections.Generic;
using System.Threading;
using contests4;
using data_processors;

namespace ConsoleApplication1
{
	//~~~ top-level reactor type for Thrift message (ContestsType object) represents a root of the tree for the whole message on a given amqp queue stream(ish)
	public class thrift_contests_type : ContestsType {
		public ContestsType output_msg = new ContestsType();
		public override void Read(Thrift.Protocol.TProtocol proto) 
		{
			base.Read(proto);	
			if (modified_flag == true) {
				output_msg.set_from((ContestsType)this); // clones by value with additional sensitivity to delta-state w.r.t. previous 'output_msg' contents

				HashSet<string> rejected_contests;
				var contests = output_msg.get_contest_as_fragile();
				if (contests != null)
					rejected_contests = new HashSet<string>(contests.Keys);
				else
					rejected_contests = new HashSet<string>();
				var meetings = output_msg.get_meeting_as_fragile();
				if (meetings != null) foreach (var meeting in meetings.ToList()) {
					MeetingType msg_meeting = null;
					var markets = meeting.Value.get_markets_as_fragile();
					if (markets != null) foreach (var market in markets.ToList()) {
						var type = market.Value.get_type();
						if (type != null && type.Value == BetTypeEnum.BetTypeEnum_WIN) {
							var legs = market.Value.get_legs();
							if (legs != null) foreach (var leg in legs)
								rejected_contests.Remove(leg.Value);
						} else {
							meeting.Value.set_markets_element(market.Key, null);
							if (msg_meeting == null)
								msg_meeting = base.get_meeting_element(meeting.Key);
							msg_meeting.get_markets_element(market.Key).skip_on_read_flag = true;
						}
					}
					if (meeting.Value.get_markets() == null) {
						output_msg.set_meeting_element(meeting.Key, null);
						if (msg_meeting == null)
							msg_meeting = base.get_meeting_element(meeting.Key);
						msg_meeting.skip_on_read_flag = true;
					}
				}
				foreach (var rc in rejected_contests) {
					output_msg.set_contest_element(rc, null);
					base.get_contest_element(rc).skip_on_read_flag = true;
				}
				contests = output_msg.get_contest_as_fragile();
				if (contests != null) foreach (var contest in contests.ToList()) {
					ContestType msg_contest = null;
					var markets = contest.Value.get_markets_as_fragile();
					if (markets != null) {
						foreach (var market in markets.ToList()) {
							var type = market.Value.get_type();
							if (type == null || type.Value != BetTypeEnum.BetTypeEnum_WIN) {
								contest.Value.set_markets_element(market.Key, null);
								if (msg_contest == null)
									msg_contest = base.get_contest_element(contest.Key);
								msg_contest.get_markets_element(market.Key).skip_on_read_flag = true;
							}
						}
					}
					if (contest.Value.get_markets() == null) {
						output_msg.set_contest_element(contest.Key, null);
						if (msg_contest == null)
							msg_contest = base.get_contest_element(contest.Key);
						msg_contest.skip_on_read_flag = true;
					}
				}
			}
		}
	}//```

	//~~~ ContestsType (top-level thrift message object) instantiator  (also takes account of existing routing_keys/queues)
	public class type_factory : contests4.type_factory { 
		public override data_processors.message_base
		from_type_name(string name, string routing_key)
		{
			if (name == "ContestsType")
				return new thrift_contests_type();
			else
				return base.from_type_name(name, routing_key);
		}
	}//```

	public class Program {

		public static void Main(string[] args)
		{
			try {
				{
					var waypoint = new waypoint();
					waypoint.set_tag("test.transform.example.1.c#");
					waypoints wp = new waypoints();
					var synapse_subscriber = new data_processors.synapse_client<data_processors.waypoints>("10.16.10.127");
					var synapse_publisher = new data_processors.synapse_client<data_processors.waypoints>("10.16.10.127");
					ReaderWriterLockSlim rwl = new ReaderWriterLockSlim();
					synapse_subscriber.subscribe(rwl, new type_factory(), 
						//"test.betfair.contests.au", 
						"test.sim.*.*", 
						data_processors.synapse_client_utils.timestamp(new DateTime(2015, 1, 1)), 
						0, // until 
						data_processors.synapse_client_utils.timespan(new TimeSpan(20, 0, 0)), // delayed delivery tolerance 
						true);

					uint i = 0;
					for (var msg_wrapper = synapse_subscriber.next(7000); msg_wrapper != null; msg_wrapper = synapse_subscriber.next(5000)) {
						if (msg_wrapper.type_name == "ContestsType" && msg_wrapper.non_delta_seen == true) {
							var now = DateTime.UtcNow;
							waypoint.set_timestamp(data_processors.federated_serialisation.utils.EncodeDateTime(now));
							wp.set_from((waypoints)msg_wrapper.waypoints);
							wp.add_path_element(waypoint);
							var written_bytes = synapse_publisher.publish(msg_wrapper.amqp.routing_key + ".transformation_test_c_sharp", (ContestsType)((thrift_contests_type)msg_wrapper.msg).output_msg, wp, (i++ % 100 == 0 ? false : true), null, 0, synapse_client_utils.timestamp(now));
							Console.WriteLine("republished message of " + written_bytes + " bytes");

						}
					}

					synapse_subscriber.close();
					synapse_publisher.close();

					Console.WriteLine("bye bye");
				}
			} catch (Exception e) {
				Console.WriteLine("oops " + e.Message);
			}
		}
	}
}
