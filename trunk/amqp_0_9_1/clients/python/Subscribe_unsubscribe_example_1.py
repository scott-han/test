import sys
import threading
from twisted.internet import defer, reactor, protocol, task

from Data_processors.Federated_serialisation.Version import *
from Data_processors.Synapse.Client import *
from Data_processors.Federated_serialisation.Schemas.Contests4.ttypes import *
from Data_processors.Federated_serialisation.Utilities import *

Synapse_subscription_options = Data_processors.Synapse.Client.Subscriber.Subscription_options;
Synapse_subscriber = Data_processors.Synapse.Client.Subscriber;
Synapse_publisher = Data_processors.Synapse.Client.Publisher

Contests_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests
Message_factory_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Basic_message_factory

"""
This may not be needed, but depends on whether clients would want to use Twisted with threads and things like 'reactor.suggestThreadPoolSize(...)
"""
Lock = threading.Lock()

Quit_initiated = False

# todo more correctness please (like scoped lock, if there is such thing in Python 2.7)
def Quit() :
	Lock.acquire()
	try :
		global Quit_initiated
		if Quit_initiated == False :
			Quit_initiated = True
			reactor.callFromThread(reactor.stop)
	finally :
		Lock.release()

Synapse_host = "localhost"
Synapse_port = 5672

def Create_contests(i) :
	Contests = Contests_type()
	Contest = Contests.Set_contest_element("Some_contest_id_key")
	Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
	Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
	Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
	Contest.Set_contestName(Basic_shared_text("Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name Some contest name " + str(i)))
	return Contests


"""
Subscribing activity. Sequential yet overlapping-with-other-loops/async behaviour.
"""
@defer.inlineCallbacks
def Run():
	try : 
		# firstly populate some topics data
		print "Seeding..."
		Publisher = Synapse_publisher()
		yield Publisher.Open(Synapse_host, Synapse_port)

		for i in range(10000) :
			Waypoints = Basic_waypoints_factory().Create()
			Waypoint = Waypoints.Add_path_element()
			Test = Waypoints.Get_path()
			Waypoints.Set_path(Test)
			Waypoint.Add_inPath_element(0);
			Waypoint.Add_inPath_element(1);
			Waypoint = Waypoints.Add_path_element()
			Waypoint.Add_inPath_element(2);
			yield Publisher.Publish(str(os.getpid()) + "test.python.a", Create_contests(i), Waypoints, False, None, 0, 0, 10 * 1024 * 1024)
			yield Publisher.Publish(str(os.getpid()) + "test.python.b", Create_contests(i), None, False, None, 0, 0, 10 * 1024 * 1024)
			yield Publisher.Publish(str(os.getpid()) + "test.python.c", Create_contests(i), Waypoints, False, None, 0, 0, 10 * 1024 * 1024)
			yield Publisher.Publish(str(os.getpid()) + "test.shmython.a", Create_contests(i), None, False, None, 0, 0, 10 * 1024 * 1024)
		print "... seeded"


		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 55)
		yield Subscriber.Subscribe(str(os.getpid()) + "test.python.*", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))
		yield Subscriber.Subscribe(str(os.getpid()) + "test.shmython.a", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))
		yield Subscriber.Subscribe("bf.api.gpau.dev", Synapse_subscription_options(Begin_utc = 1, Supply_metadata = True))

		Messages_size = 0
		while (True) :
			Message_envelope = yield Subscriber.Next(55, True, True)
			if Message_envelope == None :
				print "Got timeout"
				break

			else:

				print "Got message. Routing_key(=" + Message_envelope.Amqp.Routing_key + "); Server timestamp(=" + str(Synapse_timestamp_to_datetime(Message_envelope.Amqp.Decode_timestamp())) + ")"
				Messages_size += 1
				print ">>> Content : " + repr(Message_envelope.Message)
				print ">>> Content_Exported_Dom : " + repr(Message_envelope.Message.Export_Dom())
				print ">>> Delta_Message : " + repr(Message_envelope.Delta_Message)
				#print ">>> Non-delta messages processed so far: " + str(Messages_size)

				if Messages_size == 100 :
					print ">>> Unsubscribing.. "
					Subscriber.Unsubscribe(str(os.getpid()) + "test.python.b")
					Subscriber.Unsubscribe(str(os.getpid()) + "test.shmython.*")

		print "Done with the subscribing loop." 

		Subscriber.Close()
		Subscriber = None

	except Exception, e :
		print "Error: " + str(e)

	finally :
		Quit()

if __name__ == '__main__':
	print "Starting... codegen version info: " + Data_processors.Federated_serialisation.Version.Information_String
	reactor.callWhenRunning(Run)
	reactor.run()
	print "... bye bye."
