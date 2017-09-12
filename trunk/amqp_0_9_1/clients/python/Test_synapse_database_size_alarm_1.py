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

Exit_code = 0

# todo more correctness please (like scoped lock, if there is such thing in Python 2.7)
def Quit() :
	Lock.acquire()
	try :
		sys.stdout.flush()
		global Exit_code
		Exit_code = 2 
		reactor.callFromThread(reactor.callLater, 1, reactor.stop)
	finally :
		Lock.release()

Synapse_host = "localhost"
Synapse_port = 5672

Messages_to_publish = -1

@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port, Messages_to_publish
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)

		if Messages_to_publish == 0 :
			yield Publisher.Publish("test.py", None, None, False, None, 0, 0, 100 * 1024 * 1024)
			D = defer.Deferred()
			reactor.callLater(35, D.callback, None)
			yield D
			Messages_to_publish = 10

		print "Messages to publish " + str(Messages_to_publish)
		i = 0
		while i != Messages_to_publish :
			i += 1
			Contests = Contests_type()
			Contest = Contests.Set_contest_element("Some_contest_id_key")
			Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
			Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
			Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
			Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i)))
			for j in range(10) :
				Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
			yield Publisher.Publish("test.py", Contests, None, False, None, 0, 0)
			if i % 1000 == 0 :
				print "Publishing progress... message no: " + str(i + 1) + " out of " + str(Messages_to_publish) + " messages per topic"
				sys.stdout.flush()

			# Special case -- testing of not being able to publish even 1 more message
			# sleep and try to publish again to induce the exception (connection closed etc) which will give expected error code on exit
			if Messages_to_publish == 1 :
				print "Published one message in special mode"
				sys.stdout.flush()
				D = defer.Deferred()
				reactor.callLater(5, D.callback, None)
				yield D
				print "... now will just try to trigger exception"
				sys.stdout.flush()
				i -= 1

		print "Published all messages"
		reactor.callFromThread(reactor.callLater, 1, reactor.stop)

	except Exception, e :
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)
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
		elif Arg == "--mode" :
			i += 1
			if (sys.argv[i] == "pre"):
				Messages_to_publish = 2000000000
			elif (sys.argv[i] == "post"):
				Messages_to_publish = 0
			elif (sys.argv[i] == "one"):
				Messages_to_publish = 1
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	reactor.callWhenRunning(Run_publishing_loop)

	reactor.run()
	print "Bye bye with exit code of " + str(Exit_code)
	sys.exit(Exit_code)
