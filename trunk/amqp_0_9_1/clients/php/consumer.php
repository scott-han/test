<?php

error_reporting(E_ALL);

ini_set("memory_limit", "8000000000");

require_once "vendor/autoload.php";
require_once "synapse_client.php";
//require_once "../../../../../federated_serialisation/trunk/utils/utils.php";

register_schemas(["DataProcessors\Shared"=>"../../../../../../thrift/trunk/src/php", "DataProcessors\Contests4"=>"../../../../../../thrift/trunk/src/php", "DataProcessors\Waypoints"=>"../../../../../../thrift/trunk/src/php"]);

date_default_timezone_set('UTC');

class thrift_contest_reactor {
  public function
  on_new($key, $contest)
  {
    echo "\n\nCONTEST ALLOCATED\n";
  }
  public function
  on_add($key, $contest)
  {
    echo "\n\nCONTEST ADDED\n";
  }
  public function
  on_modify($key, $contest)
  {
    echo "\n\nCONTEST MODIFIED\n";
  }
  public function
  on_delete($key, $contest)
  {
    echo "\n\nCONTEST DELETED\n";
  }
}

class thrift_contests_type extends \DataProcessors\Contests4\Contests {
    public function read($input)
   {
     if ($this->modified_flag == true) {
       echo "\n\nCONTESTS DATA MODIFIED\n";
     }
     parent::read($input);
   }
}

class lux_type_factory extends \DataProcessors\Contests4\type_factory {
  public function from_type_name($type_name, $routing_key)
  {
    if ($type_name == "Contests") {
			echo "\n\n>>>LUX<<< CONTESTS DATA CREATED for " . $routing_key . "\n";
      $rv = new thrift_contests_type();
      $rv->contest_inotify = new thrift_contest_reactor();
      return $rv;
    } else {
      parent::from_type_name($type_name);
    }
  }
}

class default_type_factory extends \DataProcessors\Contests4\type_factory {
  public function from_type_name($type_name, $routing_key)
  {
    if ($type_name == "Contests") {
			echo "\n\ndefault CONTESTS DATA CREATED for " . $routing_key . "\n";
      $rv = new thrift_contests_type();
      $rv->contest_inotify = new thrift_contest_reactor();
      return $rv;
    } else {
      parent::from_type_name($type_name);
    }
  }
}

function print_contest_msg($msg)
{
  foreach ($msg->get_contest() as $key=>$contest) {
    $dt = ($contest->get_startDate() !== null) ? \DataProcessors\FederatedSerialisation\utils\DecodeDate($contest->get_startDate()->get_value()) : "date-NA";
    $tm = ($contest->get_startTime() !== null) ? \DataProcessors\FederatedSerialisation\utils\DecodeTime($contest->get_startTime()->get_value()) : "time-NA";
    if ($contest->get_actualStart() !== null)
      $ts = DecodeTimestamp($contest->get_actualStart()->get_value());
    else
      $ts = '';
    if ($contest->get_location() != null) {
      $locDescr = str_pad($contest->get_location()->get_name(),20);
      $locDescr .= str_pad($contest->get_location()->get__ID(),4) . ' ';
      if ($contest->get_location()->get_field() !== null)
        $locDescr .= str_pad($contest->get_location()->get_field()->get__ID(),4);
      else
        $locDescr .= '    ';
    }
    else
      $locDescr = '                             ';
    echo $contest->get__ID() . " " . " " . $dt . " " . $tm . " " .
      $locDescr . " R" . str_pad($contest->get_contestNumber(),2) . " actual=" . $ts . "\n";

    if ($contest->get_participants() !== null) {
      foreach ($contest->get_participants() as $pkey=>$participant) {
        echo "  PARTICIPANT: " .  " " . $participant->get_number() . "\n";
        if ($participant->get_entities() !== null) {
          if ($participant->get_entities()->get_horse() !== null) {
            foreach ($participant->get_entities()->get_horse() as $hkey=>$horse) {
              echo "        HORSE: " . $horse->get_name() . "\n";
            }
          }
          if ($participant->get_entities()->get_dog() !== null) {
            foreach ($participant->get_entities()->get_dog() as $dkey=>$dog) {
              echo "          DOG: " . $dog->get_name() . "\n";
            }
          }
          if ($participant->get_entities()->get_jockey() !== null) {
            foreach ($participant->get_entities()->get_jockey() as $jkey=>$jockey) {
              echo "       JOCKEY: " . $jockey->get_name() . "\n";
            }
          }
        }
      }
    }
  }
}

try {
  $client = new synapse_client("DataProcessors\Waypoints\waypoints", '127.0.0.1', 5672, 'guest', 'guest');
  $client->add_subscription("/.*?lux.*/", new lux_type_factory());
  $client->add_subscription("/.*/", new default_type_factory());
  $client->subscribe("test.*",
		// from
		synapse_client_utils::timestamp_from_datetime(new DateTime("2015-01-01")), 
		// to
		null, 
		//---
		// delayed delivery tolerance
		null, 
		// synapse_client_utils::timespan_from_dateinterval(new DateInterval("PT10H")),
		//---
		// supply metadata
		true
	);

  $callbacks_attached = false;
  for ($msg_envelope = $client->next(); $msg_envelope != null; $msg_envelope = $client->next()) {
    echo ">>>>>CURRENT CONTESTS<<<<<\n";
    ksort($client->previous_messages);
    foreach ($client->previous_messages as $subject=>$types) {
      ksort($types);
      foreach ($types as $msg_type=>$msg_delta) {
        if ($msg_type == "Contests") {
          ksort($msg_delta);
          foreach ($msg_delta as $delta=>$wrapper) {
            echo "subject " . $subject . ", delta " . $delta . "\n";
            if ($wrapper->non_delta_seen == true) { // default to printing only completed (not partial-accumulation only, incomplete) messages
              if (isset($wrapper->waypoints))
                foreach($wrapper->waypoints->get_path() as $waypoint)
                  echo "waypoint at(=" . $waypoint->get_tag() . "), UTC(=" . \DataProcessors\FederatedSerialisation\utils\DecodeTimestamp($waypoint->get_timestamp()) . ")\n";
              $msg = $wrapper->msg;
              print_contest_msg($msg);
            }
          }
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
