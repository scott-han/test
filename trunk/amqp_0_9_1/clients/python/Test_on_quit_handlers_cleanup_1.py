import sys
import threading
from twisted.internet import defer, reactor, protocol, task

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *

Synapse_subscriber = Data_processors.Synapse.Client.Subscriber;
Synapse_publisher = Data_processors.Synapse.Client.Publisher

Contests_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests

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
Publishing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port
	try :
		for i in range(350) :
			print "Pub, round: " + str(i)
			Publisher = Synapse_publisher()
			yield Publisher.Open("localhost", 5672, 25)

			for j in range(10) :
				Contests = Contests_type()
				Contest = Contests.Set_contest_element("Some_contest_id_key")
				Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
				Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
				Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
				Contest.Set_contestName(Basic_shared_text("Some contest name " + str(j)))
				yield Publisher.Publish(str(os.getpid()) + ".test.py." + str(i), Contests)

			if i % 2 == 0 :
				yield Publisher.Close()

		print "Done with the publishing loop"
		Quit(0)
	except Exception, e :
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."
		Quit(-1)

if __name__ == '__main__':
	print "Starting ..."
	reactor.callWhenRunning(Run_publishing_loop)
	reactor.run()
	print "Bye bye."
	sys.exit(Exit_code)
