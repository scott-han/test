using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using contests4;
using data_processors;
using data_processors.federated_serialisation;

namespace producer
{
	class Program
	{
		static void Generate1(synapse_client<data_processors.waypoints> client, string source, uint yy, uint mm, uint dd) {
			var contests = new ContestsType();

			var contest = new ContestType();
			contest.set_competition("Australian Racing");
			contest.set_contestNumber(1);
			contest.set_contestName(new TextType()).set_Value("Drink XXXX Responsibly Sprint");
			contest.set_sportCode(SportEnum.SportEnum_gp);
			contest.set_datasource(source);
			contest.set_startDate(new DateType()).set_Value(utils.EncodeDate(yy, mm, dd));
			////
			var participant = new ParticipantType();
			participant.set_number("1");
			participant.set_barrier(new IntegerType()).set_Value(3);
			var entities = new EntitiesType();
			var horse = entities.set_horse_element("horse", new HorseType());
			horse.set_name("Small Runner");
			horse.set_countryBorn(new TextType()).set_Value("AUS");
			var jockey = entities.set_jockey_element("jockey", new PersonType());
			jockey.set_name("S Clipperton");
			jockey.set_sid("S Clipperton");
			participant.set_entities(entities);
			contest.set_participants_element("1", participant);
			////
			participant = new ParticipantType();
			participant.set_number("1A");
			participant.set_barrier(new IntegerType()).set_Value(2);
			entities = new EntitiesType();
			entities.set_horse_element("horse", new HorseType()).set_name("Medium Runner");
			participant.set_entities(entities);
			contest.set_participants_element("1A", participant);
			////
			participant = new ParticipantType();
			participant.set_number("2");
			participant.set_barrier(new IntegerType()).set_Value(1);
			entities = new EntitiesType();
			entities.set_horse_element("horse", new HorseType()).set_name("Large Runner");
			participant.set_entities(entities);
			contest.set_participants_element("2", participant);
			//
			contests.set_contest_element(source + "|" + yy.ToString() + mm.ToString() + dd.ToString() + ";1000001", contest);

			var waypoints = new waypoints();
			var waypoint = new waypoint();
			waypoint.set_timestamp(data_processors.federated_serialisation.utils.EncodeDateTime(DateTime.UtcNow));
			waypoint.set_tag("contests_1.example.csharp.1");
			waypoints.add_path_element(waypoint);
			client.publish("test." + source, contests, waypoints, false);
		}


		static void Generate2(synapse_client<data_processors.waypoints> client) 
		{
			var contests_20150401 = new ContestsType();
			var contests_20150402 = new ContestsType();

			var qt_contest1 = new ContestType();
			qt_contest1.set_competition("Australian Racing");
			qt_contest1.set_contestNumber(1);
			qt_contest1.set_contestName(new TextType()).set_Value("Drink XXXX Responsibly Sprint");
			qt_contest1.set_sportCode(SportEnum.SportEnum_gp);
			qt_contest1.set_datasource("qt");
			qt_contest1.set_startDate(new DateType()).set_Value(utils.EncodeDate(2015,04,01));
			////
			var participant = new ParticipantType();
			participant.set_number("1");
			participant.set_barrier(new IntegerType()).set_Value(3);
			var entities = new EntitiesType();
			var horse = entities.set_horse_element("horse", new HorseType());
			horse.set_name("Small Runner");
			horse.set_countryBorn(new TextType()).set_Value("AUS");
			var jockey = entities.set_jockey_element("jockey", new PersonType());
			jockey.set_name("S Clipperton");
			jockey.set_sid("S Clipperton");
			participant.set_entities(entities);
			qt_contest1.set_participants_element("1", participant);
			////
			participant = new ParticipantType();
			participant.set_number("1A");
			participant.set_barrier(new IntegerType()).set_Value(2);
			entities = new EntitiesType();
			entities.set_horse_element("horse", new HorseType()).set_name("Medium Runner");
			participant.set_entities(entities);
			qt_contest1.set_participants_element("1A", participant);
			////
			participant = new ParticipantType();
			participant.set_number("2");
			participant.set_barrier(new IntegerType()).set_Value(1);
			entities = new EntitiesType();
			entities.set_horse_element("horse", new HorseType()).set_name("Large Runner");
			participant.set_entities(entities);
			qt_contest1.set_participants_element("2", participant);
			//
			contests_20150401.set_contest_element("qt|20150401;1000001", qt_contest1);
			client.publish("test.qt", contests_20150401, null, false, Encoding.ASCII.GetBytes("contests_20150401"));

			////////////////////////////////////////////////////////////////
			qt_contest1 = new ContestType();
			qt_contest1.set_competition("Australian Racing");
			qt_contest1.set_contestNumber(1);
			qt_contest1.set_contestName(new TextType()).set_Value("Drink XXXX Responsibly Sprint");
            qt_contest1.set_sportCode(SportEnum.SportEnum_gp);
			qt_contest1.set_datasource("qt");
			qt_contest1.set_startDate(new DateType()).set_Value(utils.EncodeDate(2015,04,02));
			////
			participant = new ParticipantType();
			participant.set_number("1");
			participant.set_barrier(new IntegerType()).set_Value(3);
			entities = new EntitiesType();
			horse = entities.set_horse_element("horse", new HorseType());
			horse.set_name("Small Runner");
			horse.set_countryBorn(new TextType()).set_Value("AUS");
			jockey = entities.set_jockey_element("jockey", new PersonType());
			jockey.set_name("S Clipperton");
			jockey.set_sid("S Clipperton");
			participant.set_entities(entities);
			qt_contest1.set_participants_element("1", participant);
			////
			participant = new ParticipantType();
			participant.set_number("1A");
			participant.set_barrier(new IntegerType()).set_Value(2);
			entities = new EntitiesType();
			entities.set_horse_element("horse", new HorseType()).set_name("Medium Runner");
			participant.set_entities(entities);
			qt_contest1.set_participants_element("1A", participant);
			////
			participant = new ParticipantType();
			participant.set_number("2");
			participant.set_barrier(new IntegerType()).set_Value(1);
			entities = new EntitiesType();
			entities.set_horse_element("horse", new HorseType()).set_name("Large Runner");
			participant.set_entities(entities);
			qt_contest1.set_participants_element("2", participant);
			//
			contests_20150402.set_contest_element("qt|20150402;1000001", qt_contest1);
			client.publish("test.qt", contests_20150402, null, false, Encoding.ASCII.GetBytes("contests_20150402"));
			////////////////////////////////////////////////////////////////

			////// Jockey change - publish delta /////
			var a_contest = contests_20150401.get_contest_element("qt|20150401;1000001");
			if (a_contest != null) {
				var a_participant = a_contest.get_participants_element("1");
				if (a_participant != null) {
					var a_entities = a_participant.get_entities();
					if (a_entities != null) {
						var a_jockey = a_entities.get_jockey_element("jockey");
						if (a_jockey != null) {
							a_jockey.set_name("D J Browne");
							a_jockey.set_sid("4317");
						} else
							Console.WriteLine("Jockey not found!");
					}
					else
						Console.WriteLine("Entities not found!");
				}
				else
					Console.WriteLine("Participant 1 not found!");
			}
			else
				Console.WriteLine("Contest not found!");
			client.publish("test.qt", contests_20150401, null, true, Encoding.ASCII.GetBytes("contests_20150401"));

			///////////////////////////////////////////////////////////////

			///// Jockey change - publish delta /////
			a_contest = contests_20150402.get_contest_element("qt|20150402;1000001");
			if (a_contest != null) {
				var a_participant = a_contest.get_participants_element("1");
				if (a_participant != null) {
					var a_entities = a_participant.get_entities();
					if (a_entities != null) {
						var a_jockey = a_entities.get_jockey_element("jockey");
						if (a_jockey != null) {
							a_jockey.set_name("D J Browne");
							a_jockey.set_sid("4317");
						} else
							Console.WriteLine("Jockey not found!");
					}
					else
						Console.WriteLine("Entities not found!");
				}
				else
					Console.WriteLine("Participant 1 not found!");
			}
			else
				Console.WriteLine("Contest not found!");
			client.publish("test.qt", contests_20150402, null, true, Encoding.ASCII.GetBytes("contests_20150402"));
		}

		static void Main(string[] args)
		{
			var client = new data_processors.synapse_client<data_processors.waypoints>();
			Generate1(client, "lux", 2015, 3, 30);
			Generate1(client,"lux", 2015, 3, 31);
			Generate1(client,"lux", 2015, 4, 1);
			Generate1(client,"cen", 2015, 4, 1);
			Generate2(client);
			client.close();
		}
	}
}
