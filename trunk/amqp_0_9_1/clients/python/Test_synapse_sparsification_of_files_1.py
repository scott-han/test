import sys
import traceback
import random
import threading
import datetime
# from twisted.internet import iocpreactor
# iocpreactor.install()
from twisted.internet import defer, reactor, protocol, task

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *

Synapse_subscription_options = Data_processors.Synapse.Client.Subscriber.Subscription_options;
Synapse_subscriber = Data_processors.Synapse.Client.Subscriber;
Synapse_publisher = Data_processors.Synapse.Client.Publisher

Contests_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests
Message_factory_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Basic_message_factory

"""
import signal
def Int_handler(signal, frame) :
	import pdb
	pdb.set_trace()
signal.signal(signal.SIGINT, Int_handler)
"""

"""
This may not be needed, but depends on whether clients would want to use Twisted with threads and things like 'reactor.suggestThreadPoolSize(...)
"""
Lock = threading.Lock()

Quit_initiated = False
Exit_code = 0

Loops_awaiting_fence = 0

def Raise_loops_awaiting_fence() :
	Lock.acquire()
	global Loops_awaiting_fence
	Loops_awaiting_fence += 1
	print "Currently active clients: " + str(Loops_awaiting_fence)
	Lock.release()

def Try_quit_on_lowering_of_loops_awaiting_fence() :
	Lock.acquire()
	try :
		global Loops_awaiting_fence
		Loops_awaiting_fence -= 1
		print "Currently active clients: " + str(Loops_awaiting_fence)
		if Loops_awaiting_fence == 0 :
			sys.stdout.flush()
			global Quit_initiated
			if Quit_initiated == False :
				print "Peaceful exit initiated."
				Quit_initiated = True
				reactor.callFromThread(reactor.callLater, 1, reactor.stop)
	finally :
		Lock.release()

# todo more correctness please (like scoped lock, if there is such thing in Python 2.7)
def Quit() :
	Lock.acquire()
	try :
		sys.stdout.flush()
		global Quit_initiated
		global Exit_code
		Exit_code = -1
		if Quit_initiated == False :
			Quit_initiated = True
			reactor.callFromThread(reactor.callLater, 1, reactor.stop)
	finally :
		Lock.release()

Synapse_host = "127.0.0.1"
Synapse_port = 5672

Topics_to_publish = 5
Messages_to_publish_in_topic = 230000
Publishing_is_done = False

def Calculate_subscription_range() :
	global Messages_to_publish_in_topic
	Subscribe_begin = min(long(random.random() * Messages_to_publish_in_topic) + 1, Messages_to_publish_in_topic)
	Subscribe_end = Subscribe_begin + long(random.random() * (Messages_to_publish_in_topic - Subscribe_begin))
	if Subscribe_end > Messages_to_publish_in_topic :
		Subscribe_end = Messages_to_publish_in_topic
	return (Subscribe_begin, Subscribe_end + 1)

"""
Subscribing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_subscribing_loop(Topic_name, Seconds_begin, Seconds_end, Retry_On_Error_Counter = 0):
	global Synapse_host, Synapse_port, Try_quit_on_lowering_of_loops_awaiting_fence
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 300)

		yield Subscriber.Subscribe(Topic_name, Synapse_subscription_options(Begin_utc = Seconds_begin * 1000000, Supply_metadata = True))

		Min_timestamp = 0
		Min_sequence_number = 0
		while (True) :
			try:
				Message_envelope = yield Subscriber.Next(30)
			except Exception, e :
				break
			if Message_envelope == None :
				break

			Message_timestamp = Message_envelope.Amqp.Decode_timestamp() // 1000000
			if Message_timestamp == 0 :
				raise RuntimeError("Topic " + Topic_name + " issue when subscribing, message timestamp cannot be zero")
			if Min_timestamp != 0 :
				if Min_timestamp + 1 != Message_timestamp :
					raise RuntimeError("Topic " + Topic_name + " issue when subscribing, timestamps do not progress as expected, previous_timestamp (=" + str(Min_timestamp) + ") vs currently received timestamp (=" + str(Message_timestamp) + ')')
			elif random.random() > .5 :
				"""
				Every now and then extend the subsrciption duration based on the initially received message timestamp...
				"""
				Seconds_end = min(Message_timestamp + (Seconds_end - Seconds_begin), Messages_to_publish_in_topic)
			Min_timestamp = Message_timestamp 

			Message_sequence_number = Message_envelope.Amqp.Decode_sequence_number()
			if Min_sequence_number != 0 and Min_sequence_number >= Message_sequence_number :
				raise RuntimeError("Topic " + Topic_name + " issue when subscribing, sequence numbers do not progress as expected")
			Min_sequence_number = Message_sequence_number

			if  Min_timestamp >= Seconds_end - 1 :
				break

		yield Subscriber.Close()
		Subscriber = None
		Spawn_another_subscription = True
		Lock.acquire()
		global Publishing_is_done
		if Publishing_is_done == True :
			Spawn_another_subscription = False
		Lock.release()

		if Spawn_another_subscription == True :
			Raise_loops_awaiting_fence()
			reactor.callLater(random.random() * 5 + 2, Run_subscribing_loop, Topic_name, *Calculate_subscription_range())

		Try_quit_on_lowering_of_loops_awaiting_fence()

		print "Done in subscribing loop." 

	except Exception, e :
		if Retry_On_Error_Counter < 10 :
			reactor.callLater(35, Run_subscribing_loop, Topic_name, Seconds_begin, Seconds_end, Retry_On_Error_Counter + 1)
		else :
			print str(datetime.datetime.now()) + " Error in subscribing loop: " + str(e)
			exc_type, exc_value, exc_traceback = sys.exc_info()
			traceback.print_tb(exc_traceback, None, sys.stdout)
			Quit()

"""
Publishing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port, Raise_loops_awaiting_fence, Try_quit_on_lowering_of_loops_awaiting_fence
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 300)

		for i in range(Messages_to_publish_in_topic) :
			for k in range(Topics_to_publish) :
				Contests = Contests_type()
				Contest = Contests.Set_contest_element("Some_contest_id_key")
				Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
				Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
				Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
				Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i)))
				for j in range(10) :
					Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
				Topic_name = str(os.getpid()) + ".test.py." + str(k)
				yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, (i + 1) * 1000000, 10 * 1024 * 1024)
			if i % 1000 == 0 :
				print "Publishing progress... message no: " + str(i + 1) + " out of " + str(Messages_to_publish_in_topic) + " messages per topic"
				sys.stdout.flush()

		print "Published all messages"
		Lock.acquire()
		global Publishing_is_done
		Publishing_is_done = True
		Lock.release()
		Try_quit_on_lowering_of_loops_awaiting_fence()

	except Exception, e :
		print str(datetime.datetime.now()) + " Error in publishing loop: " + str(e)
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback, None, sys.stdout)
		Quit()

if __name__ == '__main__':
	print "Starting"
	# reactor.suggestThreadPoolSize(100)

	Argc = len(sys.argv)
	i = 1
	while i < Argc :
		Arg = sys.argv[i]
		if i == Argc - 1 :
			raise RuntimeError("incorrect args, " + Arg + " ought to have an explicit value")
		elif Arg == "--synapse_at" :
			i += 1
			Arg = sys.argv[i]
			Host_port = Arg.split(':')
			Synapse_host = Host_port[0]
			if len(Host_port) > 1 :
				Synapse_port = int(Host_port[1])
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	Raise_loops_awaiting_fence()
	reactor.callWhenRunning(Run_publishing_loop)

	for k in range(Topics_to_publish) :
		Topic_name = str(os.getpid()) + ".test.py." + str(k)
		for l in range(15) :
			Raise_loops_awaiting_fence()
			reactor.callLater(random.random() * 3 + 1, Run_subscribing_loop, Topic_name, *Calculate_subscription_range())

	reactor.run()
	print "Bye bye with exit code of " + str(Exit_code)
	sys.exit(Exit_code)
