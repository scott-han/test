import sys
import traceback
import random
import threading
import time
import datetime
# from twisted.internet import iocpreactor
# iocpreactor.install()
from twisted.internet import defer, reactor

from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *
from Data_processors.Federated_serialisation.Utilities import Basic_waypoints_factory
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import Basic_message_factory as Basic_traceRoute_factory
from Data_processors.Synapse.Client import Subscriber as Synapse_subscriber, Datetime_to_synapse_timestamp

Synapse_subscription_options = Synapse_subscriber.Subscription_options;
Synapse_publisher = Data_processors.Synapse.Client.Publisher

Contests_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests
Message_factory_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Basic_message_factory

Synapse_host="localhost"
Synapse_port = 5672
Exit_code = 0

@defer.inlineCallbacks
def Sleep(Amount):
	D = defer.Deferred()
	reactor.callLater(Amount, D.callback, None)
	yield D

@defer.inlineCallbacks
def Run_subscribe_loop():
	global Exit_Code
	Publisher = Synapse_publisher()
	yield Publisher.Open(Synapse_host, Synapse_port)
	Topic_name = "a";

	Contests = Contests_type()

	Contest = Contests.Set_contest_element("Contest1")
	Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
	Contest.Set_contestName(Basic_shared_text("Contest1"))

	yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)))

	yield Publisher.Close();

	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 5)
		yield Subscriber.Subscribe("a ", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))
		while (True) :
			Message_envelope = yield Subscriber.Next(15)
			if Message_envelope is None:
				break

		Exit_code = -1

	except Exception, e :
		print "Error: " + str(e)
		print "Expected indeed..." 

	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 5)
		yield Subscriber.Subscribe("a ", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))
		while (True) :
			Message_envelope = yield Subscriber.Next()
			break

	except Exception, e :
		print "Error: " + str(e)
		print "Unexpected..." 
		Exit_code = -1

	reactor.stop()

@defer.inlineCallbacks
def Run_publish_loop() :
	global Synapse_host, Synapse_port, Raise_loops_awaiting_fence, Try_quit_on_lowering_of_loops_awaiting_fence
	try :
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 5)

		Contests = Contests_type()
		Contests.Set_datasource("Some data source")

		for i in range(10) :
			yield Publisher.Publish("a ", Contests)
			yield Sleep(1)

		Exit_code = -1

	except Exception, e :
		print "Error: " + str(e)
		print "Expected indeed..." 

	reactor.stop()


if __name__ == '__main__':
	Argc = len(sys.argv)
	i = 1
	while i < Argc :
		Arg = sys.argv[i]
		if Arg == "--Publish" :
			reactor.callWhenRunning(Run_publish_loop)
			break;
		elif Arg == "--Subscribe" :
			reactor.callWhenRunning(Run_subscribe_loop)
			break;
		else:
			raise RuntimeError("Unknown option " + Arg)
		i += 1
	reactor.run()
	sys.exit(Exit_code);
