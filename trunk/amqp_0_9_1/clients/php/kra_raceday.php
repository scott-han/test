<?php

error_reporting(E_ALL);

require_once "vendor/autoload.php";
require_once "AMQP/loader.php";
require_once "SynapseClient.php";
require_once "../../../../../federated_serialisation/trunk/utils/utils.php";

use Icicle\Loop;
use Icicle\Coroutine\Coroutine;
use Icicle\Promise;
use DataProcessors\Synapse;
use DataProcessors\Synapse\SynapseClient;
use DataProcessors\FederatedSerialisation\Utils;

Synapse\register_schemas([
	"contests4"=>"../../../../../federated_serialisation/trunk/contesthub/gen-php", 
	"contest_update"=>"../../../../../federated_serialisation/trunk/contesthub/gen-php",
	"DataProcessors"=>"../../../../../federated_serialisation/trunk/thrift/gen-php"]);

class RaceDayFeed {

	protected $client;
	protected $topicName;

	public function __construct(string $topicName) {
		 $this->client = new SynapseClient('DataProcessors\waypoints');
		 $this->topicName = $topicName;
	}

	public function go(): \Generator {
		yield $this->client->open('127.0.0.1', 5672, 'guest', 'guest');
		echo "Opened Synapse\n";
		//
		$contests = new contest_update\contestsType();
		$contestId = '20151004;1;1;acc';
		$contest = $contests->set_contest_element($contestId);

		/* Track condition change */
		$cond = $contest->set_trackCondition();
		$cond->set_value('8');
		$cond->set_description('good');

		/* Jockey change */
		$part = $contest->set_participants_element(1);
		$part->set_barrier()->set_value(1);
		$part->set_handicapWeight()->set_value(50);
		$entities = $part->set_entities();
		$horse = $entities->set_horse_element('horse');
		$horse->set_name('Duntroon');
		$jockey = $entities->set_jockey_element('jockey');
		$jockey->set_name('J Smith');
		$jockey->set_sid(1234);
		
		/* Set waypoint info */
		$waypoints = new DataProcessors\waypoints();
		$waypoint = new DataProcessors\waypoint();
		$waypoint->set_timestamp(utils\EncodeNow());
		$waypoint->set_tag("racedayfeed.php");
		$waypoints->add_path_element($waypoint);
		
		/* Publish */
		$bytes = yield $this->client->publish($this->topicName, $contests, $waypoints);
		echo "Published contest update ($bytes payload bytes)\n";
		yield $this->client->close();
		echo "Closing\n";
	}
}

$topicName = '';
global $argv;
foreach ($argv as $arg) {
	if (preg_match('/^topic=(.*)/', $arg, $matchTopic)) {
		$topicName = $matchTopic[1];
	}
}
if ($topicName == '')
	die("topic parameter must be specified on command line");

$racedayfeed = new RaceDayFeed($topicName);
$go = new Coroutine($racedayfeed->go());

Loop\run();