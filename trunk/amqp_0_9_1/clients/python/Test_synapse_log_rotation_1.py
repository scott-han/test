import sys
import threading
import random
from twisted.internet import defer, reactor, protocol, task

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *

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

@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)

		for i in range(1000) :
			Contests = Contests_type()
			Contest = Contests.Set_contest_element("Some_contest_id_key") 
			Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
			Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
			Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
			Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i)))
			yield Publisher.Publish("test.1", Contests)
			if (i % 10) == 0:
				print "Sleeping"
				Sleeper = defer.Deferred()
				reactor.callLater(random.randint(1, 3), Sleeper.callback, None)
				yield Sleeper

		print "Done with the publishing loop"
		Quit(0)
	except Exception, e :
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."
		Quit(-1)

if __name__ == '__main__':
	print "Starting python client."

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
		else :
			raise RuntimeError("Unknown option " + Arg)
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	reactor.callWhenRunning(Run_publishing_loop)
	reactor.run()
	print "Bye bye."
	sys.exit(Exit_code)
