<?php

error_reporting(E_ALL);

ini_set("memory_limit", "8000000000");

require_once dirname(__FILE__) . "/vendor/autoload.php";
require_once dirname(__FILE__) . "/synapse_client.php";
//require_once dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/utils/utils.php";
//register_schemas(["contests4"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/contesthub/gen-php", "data_processors"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/thrift/gen-php"]);

use DataProcessors\Contests4;

date_default_timezone_set('UTC');

// at the moment is not 'whitebox' like testing... a todo for the future.

try {
	echo "publishing in tester\n";
	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');

	$contests= new Contests4\Contests();
  $contest = new Contests4\Contest();
  
	$long_string_to_counter_socket_buffering_accumulation = "a";
	// to the power of
	for ($x = 0; $x != 21; ++$x)
   $long_string_to_counter_socket_buffering_accumulation .= $long_string_to_counter_socket_buffering_accumulation;
  $contest->set_competition($long_string_to_counter_socket_buffering_accumulation);
  $contests->set_contest_element("a", $contest);

  $starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime($date_string = "2015-01-01 01:01:01.000000"));
	echo "Publishing " . $date_string . "\n";
	for ($x = 0; $x != 100; ++$x)
		$client->publish("a", $contests, null, false, null, 0, $starting_timestamp + $x);

  $starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime($date_string = "2015-02-01 01:01:01.000000"));
	echo "Publishing " . $date_string . "\n";
	for ($x = 0; $x != 1000; ++$x) 
		$client->publish("a", $contests, null, false, null, 0, $starting_timestamp + $x);

  $starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime($date_string = "2015-05-01 01:01:01.000000"));
	echo "Publishing " . $date_string . "\n";
	for ($x = 0; $x != 100; ++$x) 
		$client->publish("a", $contests, null, false, null, 0, $starting_timestamp + $x);

	$client->close();

	echo "subscribing in tester, too much into future, without preroll\n";
  $starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime("2017-01-01 01:01:01.000000"));
	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');
  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("a", $starting_timestamp);
	$total_messages_size = 0;
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next())
		++$total_messages_size;
	if ($total_messages_size > 0)
		throw new Exception("received some messages when none was expected\n");
	$client->close();

	echo "subscribing in tester, too much into future, with preroll\n";
	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');
  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("a", $starting_timestamp, null, null, false, false, true);
	$total_messages_size = 0;
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next())
		++$total_messages_size;
	if ($total_messages_size == 0)
		throw new Exception("didnt receive any messages when some were expected\n");
	$client->close();

	echo "subscribing in tester, somewhere in the middle, without preroll\n";
  $min_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime("2015-05-01 01:01:01.000000"));
  $starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime("2015-03-01 01:01:01.000000"));
	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');
  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("a", $starting_timestamp, null, null, true, false, false);
	$total_messages_size = 0;
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		if ($min_timestamp > $msg_envelope->amqp->decode_timestamp())
			throw new Exception("received message is too far in the past (unexpected)\n");
	}
	if ($total_messages_size != 100)
		throw new Exception("didnt get the exact number of messages expected\n");
	$client->close();

  echo "bye bye\n";
	exit(0);
} catch (Exception $e) {
  echo "Exception: " . $e->getMessage() . "\n";
	exit(1);
}

?>
