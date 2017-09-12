import sys
import traceback
import random
import threading
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
	Lock.release()

def Try_quit_on_lowering_of_loops_awaiting_fence() :
	Lock.acquire()
	try :
		global Loops_awaiting_fence
		Loops_awaiting_fence -= 1
		if (Loops_awaiting_fence == 0) :
			sys.stdout.flush()
			global Quit_initiated
			if Quit_initiated == False :
				Quit_initiated = True
				reactor.callFromThread(reactor.stop)
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
			reactor.callFromThread(reactor.stop)
	finally :
		Lock.release()

Synapse_host = "localhost"
Synapse_port = 5672

Messages_to_publish_in_topic = 25000

"""
Subscribing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_subscribing_loop(Topic_name, Micros_from):
	global Synapse_host, Synapse_port, Try_quit_on_lowering_of_loops_awaiting_fence
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 15)
		yield Subscriber.Subscribe(Topic_name, Synapse_subscription_options(Begin_utc = Micros_from, Supply_metadata = True))

		Min_timestamp = 0
		Min_sequence_number = 0
		while (True) :
			Message_envelope = yield Subscriber.Next()

			Message_timestamp = Message_envelope.Amqp.Decode_timestamp()
			if (Message_timestamp == 0) :
				raise RuntimeError("Topic " + Topic_name + " issue when subscribing, message timestamp cannot be zero")
			if (Min_timestamp == 0 and Message_timestamp > Micros_from) :
				raise RuntimeError("Topic " + Topic_name + " issue when subscribing, 1st timestamp (=" + str(Message_timestamp) + ") is > subcsription start(=" + str(Micros_from) + ')')
			if (Min_timestamp != 0 and Min_timestamp + 1 != Message_timestamp) :
				raise RuntimeError("Topic " + Topic_name + " issue when subscribing, timestamps do not progress as expected, previous_timestamp (=" + str(Min_timestamp) + ") vs currently received timestamp (=" + str(Message_timestamp) + ')')
			Min_timestamp = Message_timestamp 

			Message_sequence_number = Message_envelope.Amqp.Decode_sequence_number()
			if (Min_sequence_number != 0 and Min_sequence_number >= Message_sequence_number) :
				raise RuntimeError("Topic " + Topic_name + " issue when subscribing, sequence numbers do not progress as expected")
			Min_sequence_number = Message_sequence_number

			if (Min_timestamp == Messages_to_publish_in_topic) :
				break

		Try_quit_on_lowering_of_loops_awaiting_fence()

	except Exception, e :
		print "Error: " + str(e)
		print "Done with the subscribing loop." 
		Quit()

"""
Publishing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port, Raise_loops_awaiting_fence, Try_quit_on_lowering_of_loops_awaiting_fence
	try :
		Raise_loops_awaiting_fence()
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 15)

		for i in range(Messages_to_publish_in_topic) :
			for k in range(12) :
				Contests = Contests_type()
				Contest = Contests.Set_contest_element("Some_contest_id_key")
				Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
				Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
				Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
				Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i)))
				for j in range(10) :
					Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
				Topic_name = "test.py." + str(k)
				yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, i + 1)
				if (i == int(Messages_to_publish_in_topic / 3)) :
					for l in range(10) :
						Subscribe_from = long(random.random() * Messages_to_publish_in_topic) + 1
						Raise_loops_awaiting_fence()
						reactor.callLater(random.random() * 5, Run_subscribing_loop, Topic_name, min(Subscribe_from, Messages_to_publish_in_topic))
			if (i % 1000 == 0) :
				print "Publishing progress... message no: " + str(i + 1) + " out of " + str(Messages_to_publish_in_topic) + " messages per topic"
				sys.stdout.flush()

		print "Published all messages"
		Try_quit_on_lowering_of_loops_awaiting_fence()

	except Exception, e :
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)
		Quit()

if __name__ == '__main__':
	print "Starting"

	Argc = len(sys.argv)
	i = 1
	while i < Argc :
		Arg = sys.argv[i]
		if (i == Argc - 1) :
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

	reactor.callWhenRunning(Run_publishing_loop)
	reactor.run()
	print "Bye bye."
	sys.exit(Exit_code)
