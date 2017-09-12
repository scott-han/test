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

Synapse_host = "localhost"
Synapse_port = 5672
Total_Message = 3000
# Total sending time in seconds.
Total_Time = 1800.0
Total_Loop = 1
Exit_Code = 0
Running = 0
Skip_messages_from_start_size = 0

sleep_time = Total_Time / Total_Message
@defer.inlineCallbacks
def PublishMsgs(topic):
    count = 0
    global Running
    for k in range(1):
        Publisher = Synapse_publisher()
        yield Publisher.Open(Synapse_host, Synapse_port)
        Topic_name = topic;
        for i in range(Total_Message):
            Contests = Contests_type()
            Contest = Contests.Set_contest_element("Some_contest_id_key")
            Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
            Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
            Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
            Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name 1"))
            for j in range(1):
                Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
 
            yield Publisher.Publish(Topic_name, Contests, None, False, None, i + 1)
            count = count + 1
            if (count % 100 == 0):
                print(str(id) + ": Published " + str(count) + " messages at " + str(datetime.datetime.now()))
            if (count % 7000 == 0):
                print "Sleeping"
                Sleeper = defer.Deferred()
                reactor.callLater(1.23, Sleeper.callback, None)
                yield Sleeper
        #print("Sleep 1 minutes")
        yield Publisher.Close()
        #time.sleep(60);
    Running = Running - 1
    if (Running == 0):
        reactor.stop()

@defer.inlineCallbacks
def Run_subscribing_loop(topic, id):
    try :
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open("127.0.0.1", 5672)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"

        yield Subscriber.Subscribe(topic, Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2016, 1, 1, 0, 0, 0)), Supply_metadata = True))
        sequence_num = Skip_messages_from_start_size
        global Running
        while (True) :
            global Exit_Code
            Message_envelope = yield Subscriber.Next()
            num = Message_envelope.Amqp.Decode_sequence_number();
            sequence_num = sequence_num + 1;
            if (num % 100 == 0):
                print(str(id) + ": Sequence number is " + str(num))
            if (num != sequence_num):
                Exit_Code = -1
                print("Unexpected sequence number. Wanted: " + str(sequence_num) + ", got: " + str(num))
                break
            if (sequence_num == Total_Message):
                if (Exit_Code != -1):
                    Exit_Code = 0
                break
        print(str(id) + ": Sequence number for the last record is " + str(sequence_num))
        Running = Running - 1

    except Exception, e :
        print "Error: " + str(e)

    finally :
        if (Running == 0):
            reactor.stop()

if __name__ == '__main__':
    Total_Topic = 1
    Run_As_Publisher = True
    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "Sub"):
            Run_As_Publisher = False
    if (len(sys.argv) >= 3):
        Total_Message = int(sys.argv[2]);
    if (len(sys.argv) >= 4):
        Total_Topic = int(sys.argv[3]);
    if (len(sys.argv) >= 5):
        Skip_messages_from_start_size = int(sys.argv[4]);
    Running = Total_Topic
    if (Run_As_Publisher):
        for m in range(Total_Topic):
            reactor.callFromThread(PublishMsgs, "test.topic" + str(m + 1))
    else:
        for m in range(Total_Topic):
            reactor.callFromThread(Run_subscribing_loop, "test.topic" + str(m + 1), m)
    reactor.run()
    print("Task is completed. exit code: " + str(Exit_Code))
    sys.exit(Exit_Code);
