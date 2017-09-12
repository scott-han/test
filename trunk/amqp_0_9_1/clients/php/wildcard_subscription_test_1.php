<?php

error_reporting(E_ALL);

ini_set("memory_limit", "8000000000");

require_once dirname(__FILE__) . "/vendor/autoload.php";
require_once dirname(__FILE__) . "/synapse_client.php";
//require_once dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/utils/utils.php";
//register_schemas(["contests4"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/contesthub/gen-php", "data_processors"=>dirname(__FILE__) . "/../../../../../federated_serialisation/trunk/thrift/gen-php"]);

use DataProcessors\Contests4;

date_default_timezone_set('UTC');


try {

	$starting_timestamp = synapse_client_utils::timestamp_from_datetime(new DateTime("2015-01-01 01:01:01.000000"));
	$start_x = 0;

	function 
	publish_some_data($topic_names)
	{
		echo "pub\n";
		global $start_x, $starting_timestamp;
		$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');

		$contests= new Contests4\Contests();
		$contest = new Contests4\Contest();
		$contest->set_competition("some awesome comp");
		$contests->set_contest_element("a", $contest);

		foreach ($topic_names as $name=>$blah) {
			for ($x = 0; $x != 3; ++$x)
				$client->publish($name, $contests, null, false, null, 0, $starting_timestamp + $x + $start_x);
		}
		$start_x += 3;

		$client->close();
		sleep(2);
	}

	// testing for subscribing to non-existing topics...

	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');
  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("test.*", $starting_timestamp);
	sleep(2);

	$total_messages_size = 0;
	$seen_routing_keys = array();
	$seen_routing_keys["test.a"] = 0;
	$seen_routing_keys["test.b"] = 0;
	$seen_routing_keys["test.c"] = 0;
	$seen_routing_keys["test.d"] = 0;
	publish_some_data($seen_routing_keys);
	echo "sub reading\n";
	$msg_envelope = null;
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if (
			$seen_routing_keys["test.a"] == 3 && 
			$seen_routing_keys["test.b"] == 3 &&
			$seen_routing_keys["test.c"] == 3 &&
			$seen_routing_keys["test.d"] == 3
		)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");
	sleep(2);

	// testing for subscribing when publishing to existing topics...

	$seen_routing_keys = array();
	$seen_routing_keys["test.a"] = 0;
	$seen_routing_keys["test.b"] = 0;
	$seen_routing_keys["test.c"] = 0;
	$seen_routing_keys["test.d"] = 0;
	publish_some_data($seen_routing_keys);
	echo "sub reading\n";
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if (
			$seen_routing_keys["test.a"] == 3 && 
			$seen_routing_keys["test.b"] == 3 &&
			$seen_routing_keys["test.c"] == 3 &&
			$seen_routing_keys["test.d"] == 3
		)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");
	sleep(2);

	// testing for subscribing when publishing to existing and new topics...

	$seen_routing_keys = array();
	$seen_routing_keys["test.a"] = 0;
	$seen_routing_keys["test.b"] = 0;
	$seen_routing_keys["test.c"] = 0;
	$seen_routing_keys["test.d"] = 0;
	$seen_routing_keys["test.e"] = 0;
	publish_some_data($seen_routing_keys);
	echo "sub reading\n";
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if (
			$seen_routing_keys["test.a"] == 3 && 
			$seen_routing_keys["test.b"] == 3 &&
			$seen_routing_keys["test.c"] == 3 &&
			$seen_routing_keys["test.d"] == 3 &&
			$seen_routing_keys["test.e"] == 3
		)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");

  $client->close();
	sleep(2);

	// testing for subscribing to multiple wildcards on the same client instance

	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');
  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("boom1.*", $starting_timestamp);
  $client->subscribe("boom2.*", $starting_timestamp);
	sleep(2);

	$seen_routing_keys = array();
	$seen_routing_keys["boom1.1"] = 0;
	publish_some_data($seen_routing_keys);
	echo "sub reading\n";
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if ($seen_routing_keys["boom1.1"] == 3)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");
	sleep(2);

	$seen_routing_keys = array();
	$seen_routing_keys["boom1.1"] = 0;
	$seen_routing_keys["boom2.1"] = 0;
	publish_some_data($seen_routing_keys);
	echo "sub reading\n";
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if (
			$seen_routing_keys["boom1.1"] == 3 && 
			$seen_routing_keys["boom2.1"] == 3
		)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");
  $client->close();
	sleep(2);

	// testing for subscribing to existing messages in multi-wildcarded subscription scenario

	$seen_routing_keys = array();
	$seen_routing_keys["goom1.1"] = 0;
	publish_some_data($seen_routing_keys);
	sleep(2);

	$client = new synapse_client("DataProcessors\Waypoints\waypoints", 'localhost', 5672, 'guest', 'guest');
  $client->add_subscription("/.*/", new Contests4\type_factory());
  $client->consume_timeout = 10;
  $client->subscribe("goom1.*", $starting_timestamp);
  $client->subscribe("goom2.*", $starting_timestamp);
	sleep(2);

	echo "sub reading\n";
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if ($seen_routing_keys["goom1.1"] == 3)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");
	sleep(2);

	$seen_routing_keys = array();
	$seen_routing_keys["goom1.1"] = 0;
	$seen_routing_keys["goom2.1"] = 0;
	publish_some_data($seen_routing_keys);
	echo "sub reading\n";
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
		++$total_messages_size;
		$routing_key = $msg_envelope->amqp->routing_key;
		++$seen_routing_keys[$routing_key];
		if (
			$seen_routing_keys["goom1.1"] == 3 && 
			$seen_routing_keys["goom2.1"] == 3
		)
			break;
  }
	if ($msg_envelope == null)
		throw new Exception("did not get all messages");
  $client->close();
	sleep(2);

  echo "bye bye, total messages " . $total_messages_size . "\n";
	exit(0);
} catch (Exception $e) {
  echo "Exception: " . $e->getMessage() . ", total messages " . $total_messages_size . "\n";
	exit(1);
}

?>
