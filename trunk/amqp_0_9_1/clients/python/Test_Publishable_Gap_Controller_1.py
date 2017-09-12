import sys
import traceback
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

Synapse_host = "localhost"
Synapse_port = 5672
Exit_Code = 0
Days = 0


@defer.inlineCallbacks
def Run():
	global Exit_Code
	try : 
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)
		Contests = Contests_type()
		Contest = Contests.Set_contest_element("x")
		Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
		yield Publisher.Publish("a", Contests, None, False, None, 0, Days * 1000000 * 60 * 60 * 24)
		yield Publisher.Close()

	except Exception, e :
		print "Error: " + str(e)
		"""
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)
		"""
		Exit_Code = -1

	finally :
		reactor.callFromThread(reactor.stop)


if __name__ == '__main__':
	Argc = 1
	while Argc != len(sys.argv):
		Token = sys.argv[Argc]
		Argc += 1
		if Argc == len(sys.argv):
			raise RuntimeError("Option " + Token + " must have a value.")
		if Token == "--Days":
			Days = int(sys.argv[Argc])
		else:
			raise RuntimeError("Unknown option " + Token)
		Argc += 1

	if Days == 0:
		raise RuntimeError("Must supply --Days option")

	print "Starting..."
	reactor.callWhenRunning(Run)
	reactor.run()
	print "... bye bye. Exit_Code(=" + str(Exit_Code) + ")"
	sys.exit(Exit_Code)
