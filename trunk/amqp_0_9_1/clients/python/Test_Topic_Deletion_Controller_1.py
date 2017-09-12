import sys
import traceback
import threading
from twisted.internet import defer, reactor, protocol, task

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *

Synapse_publisher = Data_processors.Synapse.Client.Publisher

Contests_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests
Message_factory_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Basic_message_factory

Synapse_host = "localhost"
Synapse_port = 5672
Exit_Code = 0
Topic_Name = ""
Perpetual = False


@defer.inlineCallbacks
def Run():
	global Exit_Code
	try : 
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)
		Contests = Contests_type()
		Contest = Contests.Set_contest_element("x")
		yield Publisher.Publish(Topic_Name, Contests)
		while Perpetual == True :
			yield Publisher.Publish(Topic_Name, Contests)
		yield Publisher.Close()

	except Exception, e :
		print "Error: " + str(e)
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)
		Exit_Code = -1

	finally :
		reactor.callFromThread(reactor.stop)


if __name__ == '__main__':
	Argc = 1
	while Argc != len(sys.argv):
		Token = sys.argv[Argc]
		if Token == "--Perpetual":
			Perpetual = True
		else:
			Argc += 1
			if Argc == len(sys.argv):
				raise RuntimeError("Option " + Token + " must have a value.")
			if Token == "--Topic_Name":
				Topic_Name = sys.argv[Argc]
			else:
				raise RuntimeError("Unknown option " + Token)
		Argc += 1

	if len(Topic_Name) == 0:
		raise RuntimeError("Must supply --Topic_Name option")

	print "Starting..."
	reactor.callWhenRunning(Run)
	reactor.run()
	print "... bye bye. Exit_Code(=" + str(Exit_Code) + ")"
	sys.exit(Exit_Code)
