<?php

error_reporting(E_ALL);

ini_set("memory_limit", "1000000000");

date_default_timezone_set('UTC');

require_once "vendor/autoload.php";
require_once "synapse_client.php";
// require_once "../../../../../federated_serialisation/trunk/utils/utils.php";

register_schemas(["DataProcessors\Shared"=>"../../../../../../thrift/trunk/src/php", "DataProcessors\Contests4"=>"../../../../../../thrift/trunk/src/php", "DataProcessors\Waypoints"=>"../../../../../../thrift/trunk/src/php"]);

function Generate1($client, $source, $yy, $mm, $dd) {
  $contests = new \DataProcessors\Contests4\Contests();
$bam = json_decode('"\u76f4"');
  $contests->set_dataprovider('Drink XXXX Responsibly Sprint '.$bam);

  $contest = new \DataProcessors\Contests4\Contest();
  $contest->set_competition('Australian Racing');
  $contest->set_contestNumber(1);
  $contest->set_contestName(new \DataProcessors\Shared\Text(['value'=>'Drink XXXX Responsibly Sprint '.$bam]));
  $contest->set_sportCode(\DataProcessors\Shared\SportEnum::SportEnum_gp);
  $contest->set_datasource($source);
  $contest->set_startDate(\DataProcessors\FederatedSerialisation\utils\MakeDate($yy,$mm,$dd));
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>'1', 'barrier'=>['value'=>3]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Small Runner', 'countryBorn'=>['value'=>'AUS']]);
  $entities->set_jockey_element('jockey', ['name'=>'S Clipperton', 'sid'=>'S Clipperton']);
  $participant->set_entities($entities);
  $contest->set_participants_element('1', $participant);
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>'1A', 'barrier'=>['value'=>2]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Medium Runner']);
  $participant->set_entities($entities);
  $contest->set_participants_element('1A', $participant);
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>2,'barrier'=>['value'=>1]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Large Runner']);
  $participant->set_entities($entities);
  $contest->set_participants_element('2', $participant);
  //
  $contests->set_contest_element($source . '|' . $yy . $mm . $dd . ';1000001', $contest);

	$waypoints = new \DataProcessors\Waypoints\waypoints();
	$waypoint = new \DataProcessors\Waypoints\waypoint();
	$waypoint->set_timestamp(\DataProcessors\FederatedSerialisation\utils\EncodeNow());
	$waypoint->set_tag("contests_1.example.php.1");
	$waypoints->add_path_element($waypoint);

  $client->publish("test." . $source, $contests, $waypoints, false);
}

function Generate2($client) {

  $contests_20150401 = new \DataProcessors\Contests4\Contests();
  $contests_20150402 = new \DataProcessors\Contests4\Contests();

  $qt_contest1 = new \DataProcessors\Contests4\Contest();
  $qt_contest1->set_competition('Australian Racing');
  $qt_contest1->set_contestNumber(1);
  $qt_contest1->set_contestName(new \DataProcessors\Shared\Text(['value'=>'Drink XXXX Responsibly Sprint']));
  $qt_contest1->set_sportCode(\DataProcessors\Shared\SportEnum::SportEnum_gp);
  $qt_contest1->set_datasource('qt');
  $qt_contest1->set_startDate(\DataProcessors\FederatedSerialisation\utils\MakeDate(2015,04,01));
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>'1', 'barrier'=>['value'=>3]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Small Runner', 'countryBorn'=>['value'=>'AUS']]);
  $entities->set_jockey_element('jockey', ['name'=>'S Clipperton', 'sid'=>'S Clipperton']);
  $participant->set_entities($entities);
  $qt_contest1->set_participants_element('1', $participant);
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>'1A', 'barrier'=>['value'=>2]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Medium Runner']);
  $participant->set_entities($entities);
  $qt_contest1->set_participants_element('1A', $participant);
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>2,'barrier'=>['value'=>1]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Large Runner']);
  $participant->set_entities($entities);
  $qt_contest1->set_participants_element('2', $participant);
  //
  $contests_20150401->set_contest_element('qt|20150401;1000001', $qt_contest1);
  $client->publish("test.qt", $contests_20150401, null, false, "contests_20150401");

  ////////////////////////////////////////////////////////////////
  $qt_contest1 = new \DataProcessors\Contests4\Contest();
  $qt_contest1->set_competition('Australian Racing');
  $qt_contest1->set_contestNumber(1);
  $qt_contest1->set_contestName(new \DataProcessors\Shared\Text(['value'=>'Drink XXXX Responsibly Sprint']));
  $qt_contest1->set_sportCode(\DataProcessors\Shared\SportEnum::SportEnum_gp);
  $qt_contest1->set_datasource('qt');
  $qt_contest1->set_startDate(\DataProcessors\FederatedSerialisation\utils\MakeDate(2015,04,02));
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>'1', 'barrier'=>['value'=>3]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Small Runner', 'countryBorn'=>['value'=>'AUS']]);
  $entities->set_jockey_element('jockey', ['name'=>'S Clipperton', 'sid'=>'S Clipperton']);
  $participant->set_entities($entities);
  $qt_contest1->set_participants_element('1', $participant);
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>'1A', 'barrier'=>['value'=>2]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Medium Runner']);
  $participant->set_entities($entities);
  $qt_contest1->set_participants_element('1A', $participant);
  ////
  $participant = new \DataProcessors\Contests4\Participant(['number'=>2,'barrier'=>['value'=>1]]);
  $entities = new \DataProcessors\Contests4\Entities();
  $entities->set_horse_element('horse', ['name'=>'Large Runner']);
  $participant->set_entities($entities);
  $qt_contest1->set_participants_element('2', $participant);
  //
  $contests_20150402->set_contest_element('qt|20150402;1000001', $qt_contest1);
  $client->publish("test.qt", $contests_20150402, null, false, "contests_20150402");

  ////////////////////////////////////////////////////////////////

  ////// Jockey change - publish delta /////
  $a_contest = $contests_20150401->get_contest_element('qt|20150401;1000001');
  if ($a_contest) {
    $a_participant = $a_contest->get_participants_element('1');
    if ($a_participant) {
      $a_entities = $a_participant->get_entities();
      if ($a_entities) {
        $a_jockey = $a_entities->get_jockey_element('jockey');
        if ($a_jockey) {
          $a_jockey->set_name('D J Browne');
          $a_jockey->set_sid('4317');
        }
        else
          echo "Jockey not found!\n";
      }
      else
        echo "Entities not found!\n";
    }
    else
      echo "Participant 1 not found!\n";
  }
  else
    echo "Contest not found!\n";
  $client->publish("test.qt", $contests_20150401, null, true, "contests_20150401");

  ///////////////////////////////////////////////////////////////

  ///// Jockey change - publish delta /////
  $a_contest = $contests_20150402->get_contest_element('qt|20150402;1000001');
  if ($a_contest) {
    $a_participant = $a_contest->get_participants_element('1');
    if ($a_participant) {
      $a_entities = $a_participant->get_entities();
      if ($a_entities) {
        $a_jockey = $a_entities->get_jockey_element('jockey');
        if ($a_jockey) {
          $a_jockey->set_name('D J Browne');
          $a_jockey->set_sid('4317');
        }
        else
          echo "Jockey not found!\n";
      }
      else
        echo "Entities not found!\n";
    }
    else
      echo "Participant 1 not found!\n";
  }
  else
    echo "Contest not found!\n";
  $client->publish("test.qt", $contests_20150402, null, true, "contests_20150402");
}

try {

	$client = new synapse_client("DataProcessors\waypoints", 'localhost', 5672, 'guest', 'guest');

  echo "Calling Generate1 (1st)\n";
  Generate1($client,'lux',2015,03,30);
  echo "Calling Generate1 (2nd)\n";
  Generate1($client,'lux',2015,03,31);
  echo "Calling Generate1 (3rd)\n";
  Generate1($client,'lux',2015,04,01);
  echo "Calling Generate1 (4th)\n";
  Generate1($client,'cen',2015,04,01);
  echo "Calling Generate2\n";
  Generate2($client);

	$client->close();

} catch (Exception $e) {
	echo "NOT sent " .$e->getMessage();
}

?>
