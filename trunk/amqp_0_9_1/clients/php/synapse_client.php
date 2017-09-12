<?php

use PhpAmqpLib\Connection\AMQPConnection;
use PhpAmqpLib\Message\AMQPMessage;

$thrift_php_lib_path = getenv("THRIFT_PHP_LIB_PATH");

// require_once __DIR__.'/../../../../../federated_serialisation/trunk/thrift/dist/lib/php/lib/Thrift/ClassLoader/ThriftClassLoader.php';
//require_once $thrift_php_lib_path.'/Thrift/ClassLoader/ThriftClassLoader.php';

use Thrift\ClassLoader\ThriftClassLoader;

$thrift_loader = new ThriftClassLoader();
function register_schemas($schemas)
{
	$GLOBALS['thrift_loader']->registerNamespace('Thrift', $GLOBALS['thrift_php_lib_path']);
	foreach ($schemas as $schema_name=>$schema_dir)
		$GLOBALS['thrift_loader']->registerDefinition($schema_name, $schema_dir);
	$GLOBALS['thrift_loader']->register();
}

// hacked version to improve on speed without spending much coding time on it...
//require_once $thrift_php_lib_path.'/Thrift/Transport/TTransport.php';
class memory_buffer_reader extends Thrift\Transport\TTransport {
  public function __construct($buf = '') {
	 if(ini_get('mbstring.func_overload') & 2)
		throw new Exception("currently unsupported in this specialised version of transport -- php.ini setting with mbstring.func_overload with value of two");
	 $this->buf_ = $buf;
	 $this->buf_size = strlen($buf);
  }
  private $buf_begin = 0;
  private $buf_size = 0;
  protected $buf_ = '';
  public function isOpen() { return true; }
  public function open() {}
  public function close() {}
  public function write($buf) {
	throw new Exception("writing is currently unsupported in this specialised version of transport");
  }
  public function read($len) {
	 if ($this->buf_size < $len || $this->buf_size === 0)
      throw new Exception('given reading of data is unsupported in this specialised version of transport');
	 $ret = substr($this->buf_, $this->buf_begin, $len);
	 $this->buf_begin += $len;
	 $this->buf_size -= $len;
	 return $ret;
  }
  public function readAll($len) {
	 if ($this->buf_size < $len || $this->buf_size === 0)
      throw new Exception('given reading of data is unsupported in this specialised version of transport');
	 $ret = substr($this->buf_, $this->buf_begin, $len);
	 $this->buf_begin += $len;
	 $this->buf_size -= $len;
	 return $ret;
  }
  function getBuffer() {
    return $this->buf_;
  }
  public function available() {
    return $this->buf_size;
  }
}

abstract class message_envelope_flags_enum {
	const has_waypoints = 1;
	const is_delta = 2;
	const has_stream_id = 8;
}

class amqp_envelope {
	public $routing_key;
	public $properties;
	public function decode_timestamp() {
		return $this->properties["timestamp"];
	}
	public function decode_sequence_number() {
    foreach ($this->properties['application_headers'] as $key=>$prop) {
      if ($key == 'XXXXXX')
        return $prop[1];
    }
    return null;
  }
	public function __construct($routing_key, $properties) {
		$this->routing_key = $routing_key;
		$this->properties = $properties;
	}
}

class message_envelope {
	public $flags;
	public $waypoints = null;
	public $msg;
	public $non_delta_seen = false;
	public $type_name;
	public $amqp;
	public function
	__construct($x)
	{
		$this->msg = $x;
	}
}

class synapse_client {
	private $waypoints_type_name;
	private $message_factories;
	private function
	find_message_factory(string $routing_key)
	{
		if (!isset($this->message_factories))
			throw new Exception("add_subscription with appropriate factory object must have been called before actually subscribing");
		foreach ($this->message_factories as $regex=>$message_factory)
			if (preg_match($regex, $routing_key))
				return $message_factory;
		throw new Exception("could not find relevant message_factory for given routing_key " . $routing_key);
	}

	private $connection;
	private $channel;

	private $publishing_channels;

	public $previous_messages;

	private $amqp_msg = null;

	public $consume_timeout = 300;

	public function
	__construct(string $waypoints_type_name, string $server_host = 'localhost', int $server_port = 5672, string $user = 'guest', string $pass = 'guest')
	{
		if (PHP_INT_SIZE != 8 || PHP_INT_MAX < 9223372036854775807)
			throw new Exception("Only PHP v>=7 on native 64-bit platform is supported");

		$this->waypoints_type_name = $waypoints_type_name;
		$this->connection = new AMQPConnection($server_host, $server_port, $user, $pass, '/', false, 'AMQPLAIN', null, 'en_US', 1200.0, 1200.0, null, false, 500);
		$this->channel = $this->connection->channel();
		$this->max_received_messages_size = 8192;
		$this->previous_messages = array();
	}

	public function
	add_subscription(string $regex, $message_factory) 
	{
		if (!isset($this->message_factories))
			$this->message_factories = array();
		$this->message_factories[$regex] = $message_factory;
	}

  public function
  getSocket()
  {
    return $this->connection->getSocket();
  }

	public function
	close()
	{
		$this->channel->close();
		if (isset($this->publishing_channels))
			foreach($this->publishing_channels as $key=>$val)
				$val->close();
		$this->connection->close();
	}

	//~~~ initiate subscription
	// expecting dates to be string type (until PHP 7 rolls out its native support for 64bit ints on windows), all in microsecs units
	public function
	subscribe($topic_name, $begin_utc = null, $end_utc = null, $delayed_delivery_tolerance = null, $supply_metadata = false, $tcp_no_delay = false, $preroll_if_begin_timestamp_in_future = false)
	{
		$dictionary = array();
		if ($begin_utc != null)
			$dictionary["begin_timestamp"] = array('l', $begin_utc);
		if ($end_utc != null)
			$dictionary["end_timestamp"] = array('l', $end_utc);
		if ($delayed_delivery_tolerance != null)
			$dictionary["delayed_delivery_tolerance"] = array('l', $delayed_delivery_tolerance);
		if ($supply_metadata == true)
			$dictionary["supply_metadata"] = array('I', 1);
		if ($tcp_no_delay == true)
			$dictionary["tcp_no_delay"] = array('I', 1);
		if ($preroll_if_begin_timestamp_in_future == true)
			$dictionary["preroll_if_begin_timestamp_in_future"] = array('I', 1);
		$on_received_message = function($amqp_msg) {
			if ($this->amqp_msg != null)
				throw new Exception("Sanity check failed... cached amqp_msg ought  to be null at this stage...");
			$this->amqp_msg = $amqp_msg;
		};
		$this->channel->basic_consume($topic_name, '', false, false, false, false, $on_received_message, null, $dictionary);
	}//```
	
	//~~~ cancel subscription
	public function
	unsubscribe($topic_name)
	{
		$this->channel->basic_cancel($topic_name, true);
	}
	//```  

	public function //~~~ get next message in stream
	next()
	{
		$rv = null;

		try {
			while ($this->amqp_msg == null)
				$this->channel->wait(null, false, $this->consume_timeout);
		} catch (\PhpAmqpLib\Exception\AMQPTimeoutException $e) {
			return null;
		}

		if (count($this->amqp_msg->body) != 0) {
			$wire = new memory_buffer_reader($this->amqp_msg->body);

			$protocol = new Thrift\Protocol\TCompactProtocol($wire);

			$protocol->readByte($version);
			if ($version != 1)
				throw new Exception("received unsupported version number");

			$protocol->readI32($flags);

			$protocol->readString($type_name);

			if ($flags & message_envelope_flags_enum::has_stream_id)
				$protocol->readString($stream_id);
			else
				$stream_id = '';

			$routing_key = $this->amqp_msg->delivery_info["routing_key"];

			if (!array_key_exists($routing_key, $this->previous_messages))
				$this->previous_messages[$routing_key] = array($type_name => array($stream_id => new message_envelope($this->find_message_factory($routing_key)->from_type_name($type_name, $routing_key))));
			if (!array_key_exists($type_name, $this->previous_messages[$routing_key]))
				$this->previous_messages[$routing_key][$type_name] = array($stream_id => new message_envelope($this->find_message_factory($routing_key)->from_type_name($type_name, $routing_key)));
			if (!array_key_exists($stream_id, $this->previous_messages[$routing_key][$type_name]))
				$this->previous_messages[$routing_key][$type_name][$stream_id] = new message_envelope($this->find_message_factory($routing_key)->from_type_name($type_name, $routing_key));

			$rv = $this->previous_messages[$routing_key][$type_name][$stream_id];

			$rv->flags = $flags;

			if ($flags & message_envelope_flags_enum::has_waypoints) {
				if (!isset($rv->waypoints))
					$rv->waypoints = new $this->waypoints_type_name();
				$rv->waypoints->read_in_delta_mode = false;
				$rv->waypoints->read($protocol);
			}
			if ($flags & message_envelope_flags_enum::is_delta) {
				$rv->msg->read_in_delta_mode = true;
			} else {
				if ($rv->non_delta_seen == false)
					$rv->non_delta_seen = true;
				$rv->msg->read_in_delta_mode = false;
			}

			$rv->msg->read($protocol);

			$rv->type_name = $type_name;
			$rv->amqp = new amqp_envelope($routing_key, $this->amqp_msg->get_properties());
		}
		$this->amqp_msg = null;
		return $rv;
	}//```

	public function Deallocate_publishing_channel($topic_name) {
	echo "DEALLOCING\n";
		if (isset($this->publishing_channels[$topic_name])) {
			$this->publishing_channels[$topic_name]->close();
		}
	}

	public function //~~~ publish
	publish($topic_name, $msg, $waypoints = null, $send_as_delta = false, $stream_id = null, $message_sequence_number = 0, $micros_since_epoch = 0)
	{
		$bytes_written = 0;
		$wire = new Thrift\Transport\TMemoryBuffer();
		$protocol = new Thrift\Protocol\TCompactProtocol($wire);

		$bytes_written += $protocol->writeByte(1);

		$flags = 0;

		if (isset($waypoints))
			$flags |= message_envelope_flags_enum::has_waypoints;

		if ($send_as_delta == true) 
			$flags |= message_envelope_flags_enum::is_delta;
		
		if ($stream_id != null)
			$flags |= message_envelope_flags_enum::has_stream_id;
		$bytes_written += $protocol->writeI32($flags);

		$bytes_written += $protocol->writeString($msg->getName());

		if ($stream_id != null)
			$bytes_written += $protocol->writeString($stream_id);

		if (isset($waypoints))
			$bytes_written += $waypoints->write($protocol);

		$bytes_written += $msg->write($protocol, !$send_as_delta);

		$protocol->getTransport()->flush();

		// publishing_channel.QueueDeclare(topic_name, false, false, false, null);
		// not too fast at the moment (bare bones example), TODO later will set topic_name elsewhere so as not to resend it everytime

		// explicit since epoch in microseconds time for the server's requirements
		$this->amqp_msg = new AMQPMessage($wire->getBuffer());
		if ($micros_since_epoch != 0 || $message_sequence_number != 0) {
			$this->amqp_msg->set("content_type", "X");
			$dictionary = array();
			$dictionary["XXXXXX"] = array("l", $message_sequence_number);
			$this->amqp_msg->set("application_headers", $dictionary);
			$this->amqp_msg->set("timestamp", $micros_since_epoch);
		}

		if (!isset($this->publishing_channels[$topic_name])) {
				$publishing_channel = $this->connection->channel();
				$this->publishing_channels[$topic_name] = $publishing_channel;
				$publishing_channel->basic_publish($this->amqp_msg, '', $topic_name);
		} else
				$this->publishing_channels[$topic_name]->basic_publish($this->amqp_msg);

		return $bytes_written;
	}//```
}

class synapse_client_utils {
	public static function
	timestamp_now()
	{
		// TODO -- deprecate in favour of 'float' version of microtime, but only after ensuring that PHP deals with doubles and not floats
		list($usec, $sec) = explode(" ", microtime(false));
		$result = gmp_init(intval($sec));
		$result = gmp_mul($result, 1000000);
		$result = gmp_add($result, intval(floatval($usec) * 1000000));
		return gmp_strval($result);
	}
	public static function
	timestamp_from_datetime($dt)
	{
		$result = gmp_init($dt->getTimestamp());
		$result = gmp_mul($result, 1000000);
		$result = gmp_add($result, $dt->format('u'));
		return gmp_strval($result);
	}
	public static function
	timestamp_to_datetime($timestamp) {
		$seconds_and_remainder = gmp_div_qr($timestamp, 1000000);
		$rv = new DateTime();
		$rv->setTimestamp(gmp_intval($seconds_and_remainder[0]));
		return new DateTime($rv->format("Y-m-d H:i:s") . "." . gmp_intval($seconds_and_remainder[1]));
	}
	public static function
	timespan_from_dateinterval($x)
	{
		$ref_begin = new DateTimeImmutable;
		$ref_end = $ref_begin->add($x);
		$ref_begin_ts = synapse_client_utils::timestamp_from_datetime($ref_begin);
		$ref_end_ts = synapse_client_utils::timestamp_from_datetime($ref_end);
		return gmp_strval(gmp_sub($ref_end_ts, $ref_begin_ts));
	}
}

?>
