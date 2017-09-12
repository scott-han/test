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
Synapse_host = "localhost"
Synapse_port = 5672
Stop = False
Do_publish = False 
Do_subscribe = False
Do_sparsify = False
Topic_name = None

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
def Sleep(Amount):
	D = defer.Deferred()
	reactor.callLater(Amount, D.callback, None)
	yield D

@defer.inlineCallbacks
def Subscribe_to_messages():
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 30)
		yield Subscriber.Subscribe(Topic_name, Synapse_subscription_options(Begin_utc = 1, Supply_metadata = True, Preroll_if_begin_timestamp_in_future = True))

		while (Stop == False) :
			try:
				Message_envelope = yield Subscriber.Next(30)
			except Exception, e :
				break
			if Message_envelope == None :
				break

		yield Subscriber.Close()
		Subscriber = None

		if Stop == False :
			yield Sleep(random.random() * 3)
			yield Subscribe_to_messages()
		else:
			print "Done in subscribing loop for " + Topic_name 
			sys.stdout.flush()

	except Exception, e :
		Quit_with_error(e)

@defer.inlineCallbacks
def Sparsify_topic():
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 30)
		yield Subscriber._DONT_USE_JUST_YET_Set_topic_sparsification(Topic_name, 0 + random.random() * 2000000, ((time.time() - 2000) + random.random() * 50000) * 1000)

		yield Subscriber.Close()
		Subscriber = None

		if Stop == False :
			yield Sleep(0.1 + random.random() * 5)
			yield Sparsify_topic()
		else:
			print "Done in sparsifying loop for " + Topic_name 
			sys.stdout.flush()

	except Exception, e :
		Quit_with_error(e)

@defer.inlineCallbacks
def Publish_messages() :
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 30)

		Contests = Contests_type()
		Contest = Contests.Set_contest_element("Some_contest_id_key")
		Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
		Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
		Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
		Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(os.getpid())))
		for j in range(10) :
			Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
		i = 0
		while (Stop == False) :
			yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, 0, 0 + random.random() * 2000000)
			i += 1
			if i % 1000 == 0 :
				print Topic_name + " Publishing progress... message no: " + str(i + 1) 
				sys.stdout.flush()
				yield Sleep(random.random())

		print "Done in publishing loop for " + Topic_name
		sys.stdout.flush()

	except Exception, e :
		Quit_with_error(e)

@defer.inlineCallbacks
def Run() :
	global Stop
	try :
		Deffereds = []
		if Do_subscribe == True:
			Deffereds.append(Subscribe_to_messages())
		if Do_sparsify == True:
			Deffereds.append(Sparsify_topic())
		if Do_publish == True:
			Deffereds.append(Publish_messages())

		yield Sleep(55)
		Stop = True

		for i in Deffereds:
			yield i

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
			Do_publish = True
		elif Arg == "--Subscribe" :
			Do_subscribe = True
		elif Arg == "--Sparsify" :
			Do_sparsify = True
		elif Arg == "--Topic_name" :
			i += 1
			if i == Argc:
				raise RuntimeError("incorrect args, " + Arg + " ought to have an explicit value")
			Topic_name = sys.argv[i]
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	reactor.callWhenRunning(Run)

	reactor.run()

	print "Bye bye with exit code of " + str(Exit_code)
	sys.stdout.flush()
	sys.exit(Exit_code)
