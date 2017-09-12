<?php
require_once "vendor/autoload.php";
require_once "AMQP/loader.php";  // this can be removed once AMQP is incorporated into composer's loader

/* calling basic_consume is additive under Synapse; message handler on subsequent basic_consume calls replace earlier ones. */
/* the following handler should get messages on both test1 and test2 */

class Demo {

  public function go() {
    $conn = new DataProcessors\AMQP\AMQPConnection();
    yield $conn->connect('127.0.0.1', 5672, 'guest', 'guest');
    $channel = yield $conn->channel();
    $handler = function($msg) {
      echo "Got a message -- routing_key=" . $msg->delivery_info["routing_key"] . "\n";
      $timestamp = $msg->properties['timestamp'];
      foreach ($msg->properties['application_headers'] as $key=>$prop) {
        if ($key == 'XXXXXX') {
          $sequenceNumber = $prop[1];
          break;
        } 
      }
      //echo "timestamp=$timestamp sequence=$sequenceNumber\n";
      file_put_contents('multisub.last', "$sequenceNumber $timestamp");
    };
    $dictionary = array();
    $dictionary["supply_metadata"] = array('I', 1);
    
    $last = explode(' ', file_get_contents('multisub.last'));
    $last_timestamp = $last[1];
    echo "Resuming from timestamp $last_timestamp\n";
    $dictionary["begin_timestamp"] = array('l', $last_timestamp);
    yield $channel->basic_consume('test1', '', false, false, false, false, $handler, $dictionary);
    yield $channel->basic_consume('test2', '', false, false, false, false, $handler, $dictionary);
  }
}

$demo = new Demo();
$coroutine = new Icicle\Coroutine\Coroutine($demo->go());

Icicle\Loop\run();
