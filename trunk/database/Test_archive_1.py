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

sleep_time = Total_Time / Total_Message
@defer.inlineCallbacks
def PublishMsgs(topic, id, repeat = 10, start = 0, span=50000):
    count = 0
    global Running
    global Exit_Code
    try:
        for k in range(1):
            Publisher = Synapse_publisher()
            yield Publisher.Open(Synapse_host, Synapse_port)
            Topic_name = topic;
            timestamp = start;
            if (start == 0):
                current = datetime.datetime.now();
                timestamp = Datetime_to_synapse_timestamp(current);

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
     
                yield Publisher.Publish(Topic_name, Contests, None, False, None, i + 1, timestamp)
                # The timestamp for each new message is drifted some seconds (60 + topic id)
                timestamp = timestamp + (60 + id) * span;
                count = count + 1
                if (count % 100 == 0):
                    print(str(id) + ": Published " + str(count) + " messages at " + str(datetime.datetime.now()))
            print("Sleep 1 minutes")
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
def Run_subscribing_loop(topic, id, span=0):
    global Running
    global Exit_Code
    try :
        if (span == 0):
            span = 50000;
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open("127.0.0.1", 5672)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"

        yield Subscriber.Subscribe(topic, Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2016, 1, 1, 0, 0, 0)), Supply_metadata = True))
        sequence_num = 0
        timestamp = 0
        while (True) :
            Message_envelope = yield Subscriber.Next(30)
            if Message_envelope is None:
              raise RuntimeError("Subscriber.Next() timeout unexpected")
            num = Message_envelope.Amqp.Decode_sequence_number();
            sequence_num = sequence_num + 1;
            msg_time = Message_envelope.Amqp.Decode_timestamp();
            # To check timestamp of the message is following our rule
            if (timestamp == 0):
                timestamp = msg_time;
            else:
                if (msg_time - timestamp == (60 + id) * span):
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
            Contests = Message_envelope.Message;
            Contest = Contests.Get_contest_element("Some_contest_id_key")
            Contest_name = Contest.Get_contestName()
            element = "Some contest name " + str(sequence_num)
            length = len(element);
            msg_length = length
            power = (id % 10) + 1;
            for i in range(power):
                msg_length = msg_length * 2;
            temp = Contest_name.Get_value();

            # To check if the content length is correct or not.
            if (len(temp) != msg_length):
                Exit_Code = -1
                break
            # To check content is correct or not
            # From the Publisher method, we know the message should like "ElementElementElement ..."
            for i in range(power):
                if (temp.find(element, i * length) != i * length):
                    Exit_Code = -1
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
    Start_time = 0
    Span = 50000
    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "Sub"):
            Run_As_Publisher = False
    if (len(sys.argv) >= 3):
        Total_Message = int(sys.argv[2]);
    if (len(sys.argv) >= 4):
        Total_Topic = int(sys.argv[3]);
    if (len(sys.argv) >= 5):
        Start_time = int(sys.argv[4]);
    if (len(sys.argv) >= 6):
        Span = int(sys.argv[5])

    Running = Total_Topic
    if (Run_As_Publisher):
        for m in range(Total_Topic):
            reactor.callFromThread(PublishMsgs, "test.topic" + str(m + 1), m, (m % 10) + 1, Start_time, Span)
    else:
        for m in range(Total_Topic):
            reactor.callFromThread(Run_subscribing_loop, "test.topic" + str(m + 1), m, Start_time)
    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);
