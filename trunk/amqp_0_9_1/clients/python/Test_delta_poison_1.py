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

Exit_code = 0

Synapse_host = "localhost"
Synapse_port = 5672

# Todo, this is a hack! (to mimic Cpp counterpart)
Total_Messages_Size = 5000

Contests_Type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Contests

Initial_Status_Value = 1
Seen_Non_Delta_Before_Poison_Status_Value = 2
Delta_Poisoned_Status_Value = 3
Done_Status_Value = 4

class Contest_Stats_Type(object) :
	def __init__(self) :
		self.Status_Progress = Initial_Status_Value 
	def Ping(self, Contest) :
		if self.Status_Progress == Initial_Status_Value and Contest.State_Of_Dom_Node.Has_Seen_Non_Delta() :
			self.Status_Progress = Seen_Non_Delta_Before_Poison_Status_Value
		elif self.Status_Progress == Seen_Non_Delta_Before_Poison_Status_Value and Contest.State_Of_Dom_Node.Is_Delta_Poison_Edge_On() :
			self.Status_Progress = Delta_Poisoned_Status_Value
		elif self.Status_Progress == Delta_Poisoned_Status_Value and Contest.State_Of_Dom_Node.Has_Seen_Non_Delta() :
			self.Status_Progress = Done_Status_Value
		elif self.Status_Progress != Done_Status_Value and Contest.State_Of_Dom_Node.Read_Counter > Total_Messages_Size * .8 :
			raise Exception("Contest reactor should probably have seen all the delta statuses in the subscription mode. Current progress status: " + str(self.Status_Progress))

class Contests_Reactor_Type(object) :
	def On_new(self, Contest_Key, Contest) :
		Contest.User_data = Contest_Stats_Type()
	def On_add(self, Contest_Key, Contest) :
		Contest.User_data.Ping(Contest)
	def On_modify(self, Contest_Key, Contest) :
		Contest.User_data.Ping(Contest)
	def On_delete(self, Contest_Key, Contest) :
		Contest.User_data.Ping(Contest)

class My_Contests_Type(Contests_Type) :
	def __init__(self) :
		Contests_Type.__init__(self)
		self.contest_inotify = Contests_Reactor_Type()

class Message_Factory_Type(object) :
	def From_type_name(self, Type_Name, Topic_Name) :
		assert Type_Name == "Contests"
		return My_Contests_Type()

@defer.inlineCallbacks
def Run():
	global Exit_code
	try : 
		Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_Factory_Type())
		yield Subscriber.Open(Synapse_host, Synapse_port, 55)
		yield Subscriber.Subscribe("test.cpp.*", Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2015, 01, 01, 0, 0)), Supply_metadata = True))

		Status_Progress = Initial_Status_Value
		while (True) :
			Message_Envelope = yield Subscriber.Next(55, True, True)
			if Message_Envelope == None :
				print "Got timeout"
				break
			else:
				Delta_Contests = Message_Envelope.Delta_Message
				Stripped_Children = Delta_Contests.Strip_Invalid_Children()
				if len(Delta_Contests._contest) == 0 :
					raise Exception("There should always be at least 1 contest.")
				if len(Stripped_Children) > 99 :
					raise Exception("Not expecting all contests to be invalid.")
				Contests = Message_Envelope.Message
				if Status_Progress == Initial_Status_Value and Contests.State_Of_Dom_Node.Has_Seen_Non_Delta() :
					Status_Progress = Seen_Non_Delta_Before_Poison_Status_Value
				elif Status_Progress == Seen_Non_Delta_Before_Poison_Status_Value and Contests.State_Of_Dom_Node.Is_Delta_Poison_Edge_On() :
					Status_Progress = Delta_Poisoned_Status_Value
				elif Status_Progress == Delta_Poisoned_Status_Value and Contests.State_Of_Dom_Node.Has_Seen_Non_Delta() :
					Status_Progress = Done_Status_Value
					# Todo, this 100 is a hack! (to mimic Cpp counterpart)
					for I in range(100) : 
						Contest = Contests.Get_contest_element(str(I))
						if Contest.User_data.Status_Progress != Done_Status_Value :
							raise Exception("Reactors are not in Done state, but should be.")
					print "Ok."
					break
				elif Status_Progress != Done_Status_Value and Contests.State_Of_Dom_Node.Read_Counter > Total_Messages_Size * .8 :
					raise Exception("Contests reactor should probably have seen all the delta statuses in the subscription mode. Current progress status: " + str(Status_Progress))

		Subscriber.Close()

	except Exception, e :
		print "Error: " + str(e)
		exc_type, exc_value, exc_traceback = sys.exc_info()
		traceback.print_tb(exc_traceback)
		Exit_code = -1

	finally :
		reactor.callFromThread(reactor.stop)

if __name__ == '__main__':
	print "Starting..."
	reactor.callWhenRunning(Run)
	reactor.run()
	print "... bye bye."
	sys.exit(Exit_code)
