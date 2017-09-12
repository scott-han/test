import sys
import traceback
import random
import threading
import time
import datetime
from datetime import timedelta

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
Stream_id = None

sleep_time = Total_Time / Total_Message
@defer.inlineCallbacks
def PublishMsgs(topic, id, repeat = 10, stream_id=None):
    count = 0
    global Running
    global Exit_Code
    try:
        for k in range(1):
            Publisher = Synapse_publisher()
            yield Publisher.Open(Synapse_host, Synapse_port)
            Topic_name = topic;
            current = datetime.datetime.now();
            timestamp = Datetime_to_synapse_timestamp(current);
            timestamp = timestamp - 100 * 24 * 60 * 60 * 1000000
            for i in range(Total_Message):
                Contests = Contests_type()
                Contest = Contests.Set_contest_element("Some_contest_id_key")
                Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
                Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
                Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
                Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i + 1)))
                # Please be advised, for this publisher, different topic has different message (both length and content)
                for j in range(repeat):
                    Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
                if i % 4 == 0: # Type name = "Contests", Stream_id = None
                    yield Publisher.Publish(Topic_name, Contests, None, False, None, i + 1, timestamp)
                elif i % 4 == 1: # Type name = "Contests", Stream_id = stream_id
                    yield Publisher.Publish(Topic_name, Contests, None, False, stream_id, i + 1, timestamp)
                elif i % 4 == 2: # Type name = "Contest", Stream_id = None
                    yield Publisher.Publish(Topic_name, Contest, None, False, None, i + 1, timestamp)
                else: # Type name = "Contest", Stream_id = stream_id
                    yield Publisher.Publish(Topic_name, Contest, None, False, stream_id, i + 1, timestamp)
                # The timestamp for each new message is drifted some seconds (60 + topic id)
                timestamp = timestamp + (600 + id) * 1000000;
                if (i % 100 == 0):
                    print(str(id) + ": Published " + str(i) + " messages at " + str(datetime.datetime.now()))
            yield Publisher.Close()
            #time.sleep(60);
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        Running = Running - 1
        if Running == 0 and reactor.running:
            reactor.stop()


@defer.inlineCallbacks
def Run_subscribing_loop(topic, id):
    global Running;
    global Exit_Code;
    try :
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open("127.0.0.1", 5672)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"

        yield Subscriber.Subscribe(topic, Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2016, 1, 1, 0, 0, 0)), Supply_metadata = True))
        sequence_num = 0
        timestamp = 0
        while (True) :
            Message_envelope = yield Subscriber.Next()
            num = Message_envelope.Amqp.Decode_sequence_number();
            sequence_num = sequence_num + 1;
            msg_time = Message_envelope.Amqp.Decode_timestamp();
            # To check timestamp of the message is following our rule
            if (timestamp == 0):
                timestamp = msg_time;
            else:
                if (msg_time - timestamp == (600 + id) * 1000000):
                    timestamp = msg_time
                else:
                    print("Message timestamp " + str(msg_time) + " is not the expected value.")
                    Exit_Code = -1
                    break
            if (num % 100 == 0):
                print(str(id) + ": Sequence number is " + str(num))
            # To check sequence number is following our rule
            if (num != sequence_num):
                Exit_Code = -1
                break
            if (sequence_num == Total_Message):
                if (Exit_Code != -1):
                    Exit_Code = 0
                break
            # To check messages include different types.
            if (sequence_num % 4 == 1 or sequence_num % 4 == 2): # See publisher code above, they should have type name "Contests"
                if (Message_envelope.Type_name != "Contests"):
                    Exit_Code = -1
                    print("Message " + str(sequence_num) + " in topic '" + topic + "' should have type name Contests")
                    break
            else: # See publisher code above, they should have type name "Contest"
                if (Message_envelope.Type_name != "Contest"):
                    Exit_Code = -1
                    print("Message " + str(sequence_num) + " in topic '" + topic + "' should have type name Contest")
                    break
            # To check message include a not None stream_id
            if (sequence_num % 4 == 2 or sequence_num % 4 == 0): # See publisher code above, they should have stream_id flag ORed.
                if not (Message_envelope.Flags & Message_envelope.HAS_STREAM_ID ):
                    Exit_Code = -1
                    print("Message " + str(sequence_num) + " in topic '" + topic + "' does not has streame_id!")
                    break
            else: # See publisher code above, they should not have stream_id flag ORed.
                if (Message_envelope.Flags & Message_envelope.HAS_STREAM_ID ):
                    Exit_Code = -1
                    print("Message " + str(sequence_num) + " in topic '" + topic + "' includes stream_id, that is not expected.")
                    break                
        print(str(id) + ": Sequence number for the last record is " + str(sequence_num))

    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        Running = Running - 1
        if Running == 0 and reactor.running:
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
        Stream_id = sys.argv[4];
    Running = Total_Topic
    if (Run_As_Publisher):
        for m in range(Total_Topic):
            reactor.callFromThread(PublishMsgs, "test.topic" + str(m + 1), m, 10, Stream_id)
    else:
        for m in range(Total_Topic):
            reactor.callFromThread(Run_subscribing_loop, "test.topic" + str(m + 1), m)
    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);
