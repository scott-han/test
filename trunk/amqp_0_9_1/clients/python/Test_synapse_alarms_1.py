import sys
import traceback
import random
import threading
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

# todo more correctness please (like scoped lock, if there is such thing in Python 2.7)
def Quit(Code) :
	Lock.acquire()
	try :
		sys.stdout.flush()
		global Quit_initiated
		global Exit_code
		if Quit_initiated == False :
			Exit_code = Code
			Quit_initiated = True
			reactor.callFromThread(reactor.callLater, 1, reactor.stop)
	finally :
		Lock.release()

Synapse_host = "localhost"
Synapse_port = 5672
Messages_to_publish = 0

@defer.inlineCallbacks
def Run_subscribing_loop():
	global Synapse_host, Synapse_port
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 15)
		yield Subscriber.Subscribe("test." + str(os.getpid()) + ".*", Synapse_subscription_options(Begin_utc = 1, Supply_metadata = True))

		while (True) :
			Message_envelope = yield Subscriber.Next(30)
			if Message_envelope == None :
				break

		print "Subscribing_loop is ending"
		sys.stdout.flush()

	except Exception, e :
		print "Error in subscribing loop: " + str(e)
		Quit(1)

@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port, Messages_to_publish
	try :

		D = defer.Deferred()
		reactor.callLater(5, D.callback, None)
		yield D

		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 15)

		while Messages_to_publish != 1 :
			if Messages_to_publish >= 1 :
				Messages_to_publish -= 1
			for k in range(8) :
				Contests = Contests_type()
				Contest = Contests.Set_contest_element("Some_contest_id_key")
				Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
				Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
				Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
				Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i)))
				for j in range(2) :
					Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
				Topic_name = "test." + str(os.getpid()) + "." + str(k)
				yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, 0, 10 * 1024 * 1024)

		print "Publishing_loop is ending"
		sys.stdout.flush()

	except Exception, e :
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."

		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)
		Quit(1)

@defer.inlineCallbacks
def Run() :

	Deffereds = []
	Deffereds.append(Run_subscribing_loop());
	Deffereds.append(Run_publishing_loop());

	for i in Deffereds:
		yield i

	print "Run is ending"
	sys.stdout.flush()

	Quit(0)

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
		elif Arg == "--Messages_to_publish" :
			i += 1
			Messages_to_publish = int(sys.argv[i])
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	reactor.callWhenRunning(Run)

	reactor.run()

	print "Bye bye with exit code of " + str(Exit_code)
	sys.exit(Exit_code)
