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
		yield Subscriber.Subscribe("test.1", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))

		while (True) :
			Message_envelope = yield Subscriber.Next()

		Quit(0)

	except Exception, e :
		print "Error: " + str(e)
		print "Done with the subscribing loop." 
		Quit(-1)

"""
Publishing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)

		for i in range(100) :
			Contests = Contests_type()
			Contest = Contests.Set_contest_element("Some_contest_id_key") 
			Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
			Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
			Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
			Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i)))
			yield Publisher.Publish("test.1", Contests)

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
		elif Arg == "--subscribe_only" :
			i += 1
			Subscribe_only = sys.argv[i]
		else :
			raise RuntimeError("Unknown option " + Arg)
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	reactor.callWhenRunning(Run_subscribing_loop)
	if Subscribe_only == 'false' :
		reactor.callLater(5, Run_publishing_loop)
	reactor.run()
	print "Bye bye."
	sys.exit(Exit_code)
