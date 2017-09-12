<?php

error_reporting(E_ALL);

ini_set("memory_limit", "1000000000");

require_once "vendor/autoload.php";
require_once "synapse_client.php";
register_schemas(array("contests4"));

date_default_timezone_set('UTC');

class weather_record_local_state {
	public $i = 0;
	public $j = 1;
	public $k = 2;
	public function
	on_modified()
	{
		echo "\n\nWEATHER RECORD DATA MODIFIED\n";
	}
	public function
	on_deleted()
	{
		echo "\n\nWEATHER RECORD DATA DELETED\n";
	}
}

function on_new_weather_record($x)
{
	echo "\n\nWEATHER RECORD DATA ADDED\n";
	$x->user_data = new weather_record_local_state();
}

class betpool_local_state {
	public $i = 0;
	public $j = 1;
	public $k = 2;
	public function
	on_modified()
	{
		echo "\n\nBETPOOL DATA MODIFIED\n";
	}
}

try {
	$client = new synapse_client("contests4");
	$client->subscribe("test.thrift.*.leon", new DateTime("2015-01-01"), null, true);

	$non_delta_seen = false;
	for ($msg = $client->next(); $msg != null; $msg = $client->next()) {
		if ($msg->get_type_id() != contests4\ImaginaryBetPool::$type_id)
			throw new TProtocolException("unexpected message type");
		echo "\nreceived raw msg, is delta: ";
		var_dump($msg->read_in_delta_mode);
		//var_dump($msg);
		if ($non_delta_seen == false && $msg->read_in_delta_mode == false) {
			$non_delta_seen = true;
			if ($msg->get_weather_records() != null) {
				// one way when dealing with structs
				//foreach(array_keys($msg->get_weather_records()) as $key) {
				//	$msg->get_weather_records_element($key)->user_data = new weather_record_local_state();
				//}
				// another way -- (saves on extra dictionary lookup(s))
				foreach($msg->get_weather_records() as $key => $value) {
					$value->user_data = new weather_record_local_state();
				}
			}
			// can call with array(obj, methodname) if classes are used...
			$msg->weather_records_on_add_callback = "on_new_weather_record";
			$msg->user_data = new betpool_local_state();
		}
		if ($non_delta_seen == true) {
			echo "\n\nreceived and parsed:" . 
				($msg->get_xid() ? "\nxid: " . $msg->get_xid() : "") . 
				($msg->get_description() ? "\ndescription: " . $msg->get_description()->get_description() : "") . 
				($msg->get_When() ? "\nWhen: " . date(DATE_RSS, $msg->get_When()->get_value()) : "") . 
				"\n";
			if ($msg->get_combinations()) {
				$sorted = $msg->get_combinations();
				ksort($sorted);
				foreach ($sorted as $key => $value) {
					$runners = unpack("C*", $key);
					echo "Runners combo [ ";
					foreach ($runners as $i)
						echo (int)$i . " ";
					echo "] probability: " . $value . "\n";
				}
			}
			if ($msg->get_weather_records() != null) {
				$sorted = $msg->get_weather_records();
				ksort($sorted);
				foreach($sorted as $j_key => $j_value) {
					echo "weather record: [".$j_key."] ".$j_value->get_xid()."\n";
					echo "weather record user_data: \n";
					var_dump($j_value->user_data);
				}
			}
		}
	}

	$client->close();
	echo "bye bye\n";
} catch (Exception $e) {
	echo "Exception: " .$e->getMessage()."\n";
}

?>
