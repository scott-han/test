import sys
import traceback
import random
import time
import threading
import thread
import datetime
# from twisted.internet import iocpreactor
# iocpreactor.install()
from twisted.internet import defer, reactor, threads, protocol, task

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *

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
Synapse_host = "localhost"
Synapse_port = 5672
Total_messages_to_publish = 33000

def Quit_with_error(e) :
	global Quit_initiated
	print str(datetime.datetime.now()) + " Error in publishing loop: " + str(e)
	exc_type, exc_value, exc_traceback = sys.exc_info()
	traceback.print_tb(exc_traceback, None, sys.stdout)
	Lock.acquire()
	try :
		sys.stdout.flush()
		Exit_code = -1
		if Quit_initiated == False :
			Quit_initiated = True
			reactor.callFromThread(reactor.callLater, 1, reactor.stop)
	finally :
		Lock.release()

@defer.inlineCallbacks
def Sparsify_topic():
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 15)
		yield Subscriber._DONT_USE_JUST_YET_Set_topic_sparsification("a", 0, Total_messages_to_publish / 2 * 1000000)

		yield Subscriber.Close()

		print "Done sparsifying" 
		sys.stdout.flush()
		reactor.callFromThread(reactor.callLater, 1, reactor.stop)

	except Exception, e :
		Quit_with_error(e)

@defer.inlineCallbacks
def Publish_messages() :
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 15)

		Contests = Contests_type()
		Contest = Contests.Set_contest_element("Some_contest_id_key")
		Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
		Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
		Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
		Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(os.getpid())))
		for j in range(15) :
			Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
		i = 0
		while i < Total_messages_to_publish :
			i += 1
			yield Publisher.Publish("a", Contests, None, False, None, 0, i * 1000000)
			if i % 1000 == 0 :
				print "Publishing progress... message no: " + str(i + 1) 
				sys.stdout.flush()

		print "Done in publishing loop"
		sys.stdout.flush()
		reactor.callFromThread(reactor.callLater, 1, reactor.stop)

	except Exception, e :
		Quit_with_error(e)


if __name__ == '__main__':
	print "Starting "
	# reactor.suggestThreadPoolSize(10)

	Argc = len(sys.argv)
	i = 1
	while i < Argc :
		Arg = sys.argv[i]
		if Arg == "--Publish" :
			reactor.callWhenRunning(Publish_messages)
		elif Arg == "--Sparsify" :
			reactor.callWhenRunning(Sparsify_topic)
		else :
			raise RuntimeError("Unknown option " + Arg)
		i += 1

	reactor.run()

	print "Bye bye with exit code of " + str(Exit_code)
	sys.stdout.flush()
	sys.exit(Exit_code)
