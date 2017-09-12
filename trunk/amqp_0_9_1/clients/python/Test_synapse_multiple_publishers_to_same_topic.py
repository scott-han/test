import sys
import traceback
import random
import threading
import time
import datetime
import random
# from twisted.internet import iocpreactor
# iocpreactor.install()
from twisted.internet import defer, reactor

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *
from Data_processors.Federated_serialisation.Utilities import Basic_waypoints_factory
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import Basic_message_factory as Basic_traceRoute_factory

Synapse_publisher = Data_processors.Synapse.Client.Publisher

Contests_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests

Synapse_host="localhost"
Synapse_port = 5672
Exit_code = 0

@defer.inlineCallbacks
def Sleep(Amount):
	D = defer.Deferred()
	reactor.callLater(Amount, D.callback, None)
	yield D

@defer.inlineCallbacks
def Run_publish_loop() :
	global Synapse_host, Synapse_port
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 20)

		Contests = Contests_type()
		Contests.Set_datasource("Some data source")
		yield Sleep(random.randint(0, 30))
		for i in range(60) :
			yield Publisher.Publish("a", Contests)
			yield Sleep(1)
			if random.randint(1,4) == 2:
				break

	except Exception, e :
		print "Error: " + str(e)
		print "Expected indeed..." 
	finally:
		
		reactor.stop()


if __name__ == '__main__':
	Argc = len(sys.argv)
	i = 1
	while i < Argc :
		Arg = sys.argv[i]
		if Arg == "--Publish" :
			reactor.callWhenRunning(Run_publish_loop)
			break;
		else:
			raise RuntimeError("Unknown option " + Arg)
		i += 1
	reactor.run()
	sys.exit(Exit_code);
