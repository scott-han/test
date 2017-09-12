<?php

error_reporting(E_ALL);

ini_set("memory_limit", "8000000000");

require_once dirname(__FILE__) . "/vendor/autoload.php";
require_once dirname(__FILE__) . "/synapse_client.php";
//require_once dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/utils/utils.php";
//register_schemas(["contests4"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/contesthub/gen-php", "data_processors"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/thrift/gen-php"]);

use DataProcessors\Contests4;

date_default_timezone_set('UTC');

function run_expectations($expected)
{
	$file = @fopen("synapse_log.txt", "r");
	while (($buffer = fgets($file, 8192)) !== false && count($expected)) {
		if (preg_match("/.*?LAZY_LOAD_LOG.*/", $buffer))
			if (preg_match($expected[0], $buffer)) {
				array_shift($expected);
			}
	}
	if (count($expected)) {
		echo "Unverified matches:\n";
		foreach ($expected as $str)
			echo $str . '\n';
		throw new Exception("expected behaviour at the server end was not verified");
	}
}

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

  $starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime("2015-01-01 01:01:01.000000"));
	for ($x = 0; $x != 1000; ++$x)
		$client->publish("a", $contests, null, false, null, 0, $starting_timestamp + $x);

	for ($x = 0; $x != 1000; ++$x) 
		$client->publish("b", $contests, null, false, null, 0, $starting_timestamp + $x);


	$client->close();

	echo "subscribing in tester\n";
	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');

  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("a", $starting_timestamp);
  $client->subscribe("b", $starting_timestamp);

	$total_messages_size = 0;
	$seen_routing_keys = array();
	$seen_routing_keys["a"] = 0;
	$seen_routing_keys["b"] = 0;
	$now = time();
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if ($seen_routing_keys["a"] > 10 || $seen_routing_keys["b"] > 10) {
			echo "now unsubscribing\n";
			$client->unsubscribe("a");
			break;
		}
		if (time() - $now > 10)
			throw new Exception("some subscribed-to topics should have been available by now");
		usleep(23000);
  }
	echo "seen before unsubscribing: \n";
	foreach($seen_routing_keys as $key=>$val)
		echo $key . "\n";


	$contiguous_b_size = 0;
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		if ($routing_key == "b")
			++$contiguous_b_size;
		else if ($routing_key == "a")
			$contiguous_b_size = 0;
  }

	if ($contiguous_b_size < 300)
		throw new Exception("could not confirm topic a being unsubscribed\n");

  $client->close();

	sleep(3);
	run_expectations(array(
		"/loaded active a/i", 
		"/loaded active b/i", 
		"/on_basic_unsubscribe a/i", 
	));

  echo "bye bye, total messages " . $total_messages_size . "\n";
	exit(0);
} catch (Exception $e) {
  echo "Exception: " . $e->getMessage() . ", total messages " . $total_messages_size . "\n";
	exit(1);
}

?>
