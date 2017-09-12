<?php 

error_reporting(E_ALL);

ini_set("memory_limit", "1000000000");

date_default_timezone_set('UTC');

require_once "vendor/autoload.php";
require_once "synapse_client.php";

register_schemas(array("contests4"));

try {

	$client = new synapse_client("contests4");

	$bet_pool = new contests4\ImaginaryBetPool();
	$bet_pool->set_weather_records_element(1, new contests4\WeatherRecordType());

	for ($x = 0; $x != 5; $x++) {
		if (!($x % 2)) {
			$bet_pool->get_weather_records_element(1)->set_xid("AAAAAAAAA");
			$weather_record = new contests4\WeatherRecordType();
			$weather_record->set_xid("there it is from php");
			$bet_pool->set_weather_records_element(3, $weather_record);
	 	} else {
			$bet_pool->set_weather_records_element(1, new contests4\WeatherRecordType());
			if (!($x % 3))
				$bet_pool->set_weather_records_element(3, null);
			else {
				$bet_pool->set_weather_records_element(3, new contests4\WeatherRecordType());
				$bet_pool->get_weather_records_element(3)->set_xid("modified xid...");
			}
		}

		$bet_pool->set_xid("11-06-2008;Sydney;1;1208%3AHK;trifecta;1;4.1;150;50;b");
		$bet_pool->set_sid("optionally set sid value:x12:1251:888");
		$bet_pool->set_description(new contests4\TextTypeExtended());
		$bet_pool->get_description()->set_description("some description from PHP publisher indeed");

		$bet_pool->set_combinations(array());
		for ($a = 0; $a != 3; $a++) {
			for ($b = 0; $b != 3; $b++) {
				for ($c = 0; $c != 3; $c++) {
					if (mt_rand() / mt_getrandmax() < 0.3) {
						$bet_pool->set_combinations_element(pack("a1a1a1", chr($a), chr($b), chr($c)), $x + mt_rand() / mt_getrandmax() * 0.777);
					}
				}
			}
		}
		$bet_pool->set_When(new contests4\TimestampType());
		$bet_pool->get_When()->set_value(time());

		// send it out ...
		$sent = $client->publish("test.thrift.hong_kong.leon", $bet_pool, $x % 10 == 0 ? false : true); // ... using inter-message delta serialistaion, with every 10th message being sent as a whole.
		echo "\n\npublished:" . 
			($bet_pool->get_xid() ? "\nxid: " . $bet_pool->get_xid() : "") . 
			($bet_pool->get_description() ? "\ndescription: " . $bet_pool->get_description()->get_description() : "") . 
			($bet_pool->get_When() ? "\nWhen: " . date(DATE_RSS, $bet_pool->get_When()->get_value()) : "") . 
			"\n";
		if ($bet_pool->get_combinations()) {
			$sorted = $bet_pool->get_combinations();
			ksort($sorted);
			foreach ($sorted as $key => $value) {
				$runners = unpack("C*", $key);
				echo "Runners combo [ ";
				foreach ($runners as $i)
					echo (int)$i . " ";
				echo "] probability: " . $value . "\n";
			}
		}
		if ($bet_pool->get_weather_records() != null) {
			$sorted = $bet_pool->get_weather_records();
			ksort($sorted);
			foreach($sorted as $j_key => $j_value) {
				echo "weather record: [".$j_key."] ".$j_value->get_xid()."\n";
				echo "weather record user_data: \n";
				var_dump($j_value->user_data);
			}
		}
	} 
	$client->close();
} catch (Exception $e) {
	echo "NOT sent " .$e->getMessage();
}

?>
