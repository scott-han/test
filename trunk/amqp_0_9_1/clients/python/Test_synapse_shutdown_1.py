import sys
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

# todo more correctness please (like scoped lock, if there is such thing in Python 2.7)
def Quit(Exit_code_) :
	Lock.acquire()
	try :
		sys.stdout.flush()
		global Quit_initiated
		global Exit_code
		if Quit_initiated == False :
			Quit_initiated = True
			Exit_code = Exit_code_
			reactor.callFromThread(reactor.stop)
	finally :
		Lock.release()

Synapse_host = "localhost"
Synapse_port = 5672

"""
Subscribing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_subscribing_loop():
	global Synapse_host, Synapse_port
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port)
		yield Subscriber.Subscribe("test.py.*", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))

		Messages_size = 0
		while (True) :
			Message_envelope = yield Subscriber.Next()

			print "Got message. Routing_key(=" + Message_envelope.Amqp.Routing_key + "); Server timestamp(=" + str(Synapse_timestamp_to_datetime(Message_envelope.Amqp.Decode_timestamp())) + ")"
			print "Contests_size (=" + str(len(Message_envelope.Message.contest)) + ")"
			for Contest in Message_envelope.Message.contest.values() :
				print "Got contest."
				if Contest.startDate and Contest.startDate.value :
					print "Starting date: " + Bitmapped_date_to_string(Contest.startDate.value)
				if Contest.startTime and Contest.startTime.value :
					print "Starting time: " + Bitmapped_time_to_string(Contest.startTime.value)
				if Contest.scheduledStart and Contest.scheduledStart.value :
					print "Scheduled start: " + Bitmapped_timestamp_to_string(Contest.scheduledStart.value)
				if Contest.contestName and Contest.contestName.value :
					print "Name: " + Contest.contestName.value
			Messages_size += 1
			print "Non-delta messages processed so far: " + str(Messages_size)
		Quit(0)

	except Exception, e :
		print "Error: " + str(e)
		print "Done with the subscribing loop." 
		Quit(-1)

def Create_contests() :
	Contests = Contests_type()
	Contest = Contests.Set_contest_element("Some_contest_id_key")
	Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
	Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
	Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
	Contest.Set_contestName(Basic_shared_text("Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name " + str(i)))
	return Contests

"""
Publishing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)

		for i in range(100000) :
			yield Publisher.Publish("test.py.1", Create_contests())
			yield Publisher.Publish("test.py.2", Create_contests())
			yield Publisher.Publish("test.py.3", Create_contests())
			yield Publisher.Publish("test.py.4", Create_contests())
			yield Publisher.Publish("test.py.5", Create_contests())
			yield Publisher.Publish("test.py.6", Create_contests())
			yield Publisher.Publish("test.py.7", Create_contests())
			yield Publisher.Publish("test.py.8", Create_contests())
			yield Publisher.Publish("test.py.a1", Create_contests())
			yield Publisher.Publish("test.py.a2", Create_contests())
			yield Publisher.Publish("test.py.a3", Create_contests())
			yield Publisher.Publish("test.py.a4", Create_contests())
			yield Publisher.Publish("test.py.a5", Create_contests())
			yield Publisher.Publish("test.py.a6", Create_contests())
			yield Publisher.Publish("test.py.a7", Create_contests())
			yield Publisher.Publish("test.py.a8", Create_contests())
			#print "Published message"

		print "Done with the publishing loop"
		Quit(0)
	except Exception, e :
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."
		Quit(-1)

if __name__ == '__main__':
	print "Starting {pub,sub}"

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

	#reactor.callWhenRunning(Run_subscribing_loop)
	reactor.callWhenRunning(Run_publishing_loop)
	reactor.run()
	print "Bye bye."
	sys.exit(Exit_code)
