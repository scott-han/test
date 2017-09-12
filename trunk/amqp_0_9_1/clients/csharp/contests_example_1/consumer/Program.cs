using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Linq;
using contests4;
using data_processors;
using data_processors.federated_serialisation;

namespace consumer
{

// this is an example of a non-callback interaction, for a callback-based interaction consider code located in tdc/contests_subscriber project

public class Program {

	public static void 
	print_contest_msg(ContestsType msg)
	{
		foreach (var contest_i in new SortedDictionary<string,ContestType>(msg.get_contest())) {
			var contest = contest_i.Value;
			var dt = utils.DecodeDate((uint)contest.get_startDate().get_Value());
			var tm = (contest.get_startTime() != null) ? utils.DecodeTime((uint)contest.get_startTime().get_Value()) : "time-NA";
			string ts;
			if (contest.get_actualStart() != null)
				ts = utils.DecodeTimestamp((ulong)contest.get_actualStart().get_Value());
			else
				ts = "";
			string locDescr;
			if (contest.get_location() != null) {
				locDescr = contest.get_location().get_name().PadRight(20);
				// NOTE -- no 'null' testing should be required if used thrift's schema marks field as required...
				locDescr += (contest.get_location().get__ID() != null ? contest.get_location().get__ID().ToString() : "").PadRight(4) + ' ';
				if (contest.get_location().get_field() != null)
					locDescr += contest.get_location().get_field().get__ID().ToString().PadRight(4);
				else
					locDescr += "    ";
			}
			else
				locDescr = "                             ";
			Console.WriteLine((contest.get__ID() != null ? contest.get__ID().ToString() : "") + " " + dt + " " + tm + " " +
				locDescr + " R" + contest.get_contestNumber().ToString().PadRight(2) + " actual=" + ts);

			if (contest.get_participants() != null) {
				foreach (var participant_i in new SortedDictionary<string,ParticipantType>(contest.get_participants())) {
					var participant = participant_i.Value;
					Console.WriteLine("  PARTICIPANT: " + " " + participant.get_number());
					if (participant.get_entities() != null) {
						if (participant.get_entities().get_horse() != null) {
							foreach (var horse_i in new SortedDictionary<string,HorseType>(participant.get_entities().get_horse())) {
								Console.WriteLine("        HORSE: " + horse_i.Value.get_name());
							}
						}
						if (participant.get_entities().get_dog() != null) {
							foreach (var dog_i in new SortedDictionary<string,DogType>(participant.get_entities().get_dog())) {
								Console.WriteLine("          DOG: " + dog_i.Value.get_name());
							}
						}
						if (participant.get_entities().get_jockey() != null) {
							foreach (var jockey_i in new SortedDictionary<string,PersonType>(participant.get_entities().get_jockey())) {
								Console.WriteLine("       JOCKEY: " + jockey_i.Value.get_name());
							}
						}
					}
				}
			}
		}
	}

	public static void Main(string[] args)
	{
		try {
			var client = new data_processors.synapse_client<data_processors.waypoints>();
			ReaderWriterLockSlim rwl = new ReaderWriterLockSlim();
			client.subscribe(rwl, new contests4.type_factory(), "test.*", 
				data_processors.synapse_client_utils.timestamp(new DateTime(2015, 1, 1)), // from
				0, // until
				data_processors.synapse_client_utils.timespan(new TimeSpan(20, 0, 0)), // delayed delivery tolerance 
				true);

			for (var msg_wrapper = client.next(); msg_wrapper != null; msg_wrapper = client.next()) {
				rwl.EnterWriteLock();
				try {
					Console.WriteLine(">>>>>CURRENT CONTESTS<<<<<");
					foreach (var messages_per_subject_i in new SortedDictionary<string, ConcurrentDictionary<string, ConcurrentDictionary<byte[], message_envelope<data_processors.waypoints>>>>(client.previous_messages)) {
						foreach (var messages_per_type_i in new SortedDictionary<string, ConcurrentDictionary<byte[], message_envelope<data_processors.waypoints>>>(messages_per_subject_i.Value)) {
							if (messages_per_type_i.Key == "ContestsType") {
								foreach (var messages_per_delta_i in new SortedDictionary<byte[], message_envelope<data_processors.waypoints>>(messages_per_type_i.Value, new data_processors.byte_array_comparator())) {
									Console.WriteLine("subject " + messages_per_subject_i.Key + ", delta " + messages_per_delta_i.Key);
									if (messages_per_delta_i.Value.non_delta_seen == true) { // default to printing only completed (not partial-accumulation only, incomplete) messages
										var msg = (ContestsType)messages_per_delta_i.Value.msg;
										if (messages_per_delta_i.Value.waypoints != null)
											foreach(var waypoint in messages_per_delta_i.Value.waypoints.get_path()) {
												Console.WriteLine("waypoint at(=" + waypoint.get_tag() + "), UTC(=" + data_processors.federated_serialisation.utils.DecodeTimestamp((ulong)waypoint.get_timestamp().Value) + ")");
											}
										print_contest_msg(msg);
									}
								}
							}
						}
					}
				} finally {
					rwl.ExitWriteLock();
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
