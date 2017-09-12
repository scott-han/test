<?php
namespace DataProcessors\Test;

error_reporting(E_ALL);

require_once "./vendor/autoload.php";

use Icicle\Loop;
use Icicle\Coroutine\Coroutine;
use Icicle\Promise;
use DataProcessors\Synapse;
use DataProcessors\Synapse\SynapseClient;
use DataProcessors\FederatedSerialisation\Utils;
use DataProcessors\Contests4;

class RaceDayFeed
{
    protected $client;
    protected $topicName;
		public $timestamp;

    public function __construct(string $topicName, int $timestamp)
    {
        $this->client = new SynapseClient('DataProcessors\Waypoints\waypoints');
        $this->topicName = $topicName;
				$this->timestamp = $timestamp;
    }

    public function go(): \Generator
    {
        yield $this->client->open('127.0.0.1', 5672, 'guest', 'guest', false, 10);
        echo "[" . date('Y-m-d H:i:s') . "] Opened Synapse " .  $this->topicName . "\n";
        //
        $contests = new Contests4\Contests();

        // Publish
        for ($i = 0; $i < 2; ++$i) {
            $bytes = yield $this->client->publish($this->topicName, $contests, null, false, null, 0, $this->timestamp);
        }
        yield $this->client->close();
        echo "[" . date('Y-m-d H:i:s') . "] Closing " . $this->topicName . "\n";
    }
}

// initial seeding
try {
	for ($i=1; $i<=100; $i++) {
			$racedayfeed1 = new RaceDayFeed('test.' . $i, 1000000);
			$go1 = new Coroutine($racedayfeed1->go());
	}
	Loop\run();
} catch (\Exception $e) {
}
// causes publishing in the past (not allowed, but server is not allowed to crash)
try {
	for ($i=1; $i<=100; $i++) {
			$racedayfeed1 = new RaceDayFeed('test.' . $i, 1000);
			$go1 = new Coroutine($racedayfeed1->go());
	}
	Loop\run();
} catch (\Exception $e) {
}
// causes publishing way too much into the future (not allowed, but server is not allowed to crash)
try {
	for ($i=1; $i<=100; $i++) {
			$racedayfeed1 = new RaceDayFeed('test.' . $i, 0);
			$go1 = new Coroutine($racedayfeed1->go());
	}
	Loop\run();
} catch (\Exception $e) {
}
