<?php

error_reporting(E_ALL);

ini_set("memory_limit", "8000000000");

require_once dirname(__FILE__) . "/vendor/autoload.php";
require_once dirname(__FILE__) . "/synapse_client.php";
//require_once dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/utils/utils.php";
//register_schemas(["contests4"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/contesthub/gen-php", "data_processors"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/thrift/gen-php"]);

use DataProcessors\Contests4;
use DataProcessors\FederatedSerialisation\Utils;
use DataProcessors\Waypoints\waypoints;


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
	$client = new synapse_client("data_processors\waypoints", 'localhost', 5672, 'guest', 'guest');

	echo "publishing in tester - c\n";
	for ($x = 0; $x != 1000; ++$x) 
		$client->publish("c", new Contests4\Contests(), null, false, null, 0, synapse_client_utils::timestamp_from_datetime(new DateTime("2015-01-01 01:01:01.000000")));

	echo "publishing in tester - b\n";
	for ($x = 0; $x != 1000; ++$x)
		$client->publish("b", new Contests4\Contests(), null, false, null, 0, synapse_client_utils::timestamp_from_datetime(new DateTime("2015-02-01 01:01:01.000000")));

	for ($x = 0; $x != 1000; ++$x)
		$client->publish("b", new Contests4\Contests(), null, false, null, 0, synapse_client_utils::timestamp_from_datetime(new DateTime("2015-03-01 01:01:01.000000")));

	for ($x = 0; $x != 1000; ++$x)
		$client->publish("b", new Contests4\Contests(), null, false, null, 0, synapse_client_utils::timestamp_from_datetime(new DateTime("2015-04-01 01:01:01.000000")));


	echo "publishing in tester - a\n";
	for ($x = 0; $x != 1000; ++$x)
		$client->publish("a", new Contests4\Contests(), null, false, null, 0, synapse_client_utils::timestamp_from_datetime(new DateTime("2015-04-01 01:01:01.000000")));

	$client->close();

	echo "subscribing in tester - a\n";
	$client = new synapse_client("data_processors\waypoints", 'localhost', 5672, 'guest', 'guest');

  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("*.#",
      synapse_client_utils::timestamp_from_datetime(new DateTime("2015-01-01")), // from
      synapse_client_utils::timestamp_from_datetime(new DateTime("2019-01-01")), // to
      synapse_client_utils::timespan_from_dateinterval(new DateInterval("PT10H")), // delayed delivery tolerance
      true);

	$seen_routing_keys = array();
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		$routing_key = $msg_envelope->amqp->routing_key;
		if (!array_key_exists($routing_key, $seen_routing_keys)) {
			$seen_routing_keys[$routing_key] = 1;
			sleep(2);
			if ($routing_key == 'c') {
				run_expectations(array("/loaded active a/i", "/loaded active b/i", "/postponing a/i", "/loaded active c/i", "/postponing b/i"));
			} else if ($routing_key == 'b') {
				run_expectations(array("/loaded active a/i", "/postponing a/i"));
				run_expectations(array("/loaded active c/i", "/retiring c/i"));
				run_expectations(array("/loaded active b/i", "/postponing b/i", "/activating postponed b/i", "/loaded active b/i"));
			}
		}
  }

  $client->close();

	sleep(3);

	run_expectations(array(
		"/loaded active a/i", 
		"/loaded active b/i", 
		"/postponing a/i", 
		"/loaded active c/i", 
		"/postponing b/i", 
		"/activating postponed b/i",
		"/retiring c/i",
		"/loaded active b/i", 
		"/activating postponed a/i",
		"/loaded active a/i"
	));

	run_expectations(array(
		"/loaded active b/i",
		"/postponing b/i",
		"/loaded active b/i",
		"/postponing b/i",
		"/loaded active b/i"
	));

  echo "bye bye\n";
	exit(0);
} catch (Exception $e) {
  echo "Exception: " .$e->getMessage()."\n";
	exit(1);
}

?>
