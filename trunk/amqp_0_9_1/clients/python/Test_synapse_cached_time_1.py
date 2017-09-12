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

Exit_code = 0

Synapse_host = "localhost"
Synapse_port = 5672

Messages_to_publish = 100000

@defer.inlineCallbacks
def Run_publishing_loop() :
	global Synapse_host, Synapse_port, Raise_loops_awaiting_fence, Try_quit_on_lowering_of_loops_awaiting_fence
	try :

		Contests = Contests_type()
		Contests.Set_datasource("aoeu")
		Topic_name = str(os.getpid()) + ".test"

		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port, 100)

		for i in range(Messages_to_publish) :
			yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, 0, 10 * 1024 * 1024)

	except Exception, e :
		global Exit_code
		Exit_code = -1
		print "Error: " + str(e)
		print "Exception in Run_publishing_loop."
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)

	reactor.callFromThread(reactor.callLater, 1, reactor.stop)

if __name__ == '__main__':
	print "Starting"

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
		i += 1

	print "Synapse at: " + Synapse_host + ':' + str(Synapse_port)
	sys.stdout.flush()

	reactor.callWhenRunning(Run_publishing_loop)
	reactor.run()
	print "Bye bye with exit code of " + str(Exit_code)
	sys.exit(Exit_code)
