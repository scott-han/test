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
Total_Message = 240000
Exit_Code = 0
Running = 0

@defer.inlineCallbacks
def PublishMsgs(timestamp):
    count = 0
    global Running
    global Total_Message;
    global Exit_Code
    try:
        for k in range(1):
            Publisher = Synapse_publisher()
            yield Publisher.Open(Synapse_host, Synapse_port)
            Topic_name = "test.topic";
            span = 1000000;
            start = 1470000000;
            if (timestamp != 0):
                start = timestamp;
            timestamp = start * 1000000;

            for i in range(Total_Message):
                Contests = Contests_type()
                Contest = Contests.Set_contest_element("Some_contest_id_key")
                Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
                Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
                Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
                Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i + 1)))
                # Please be advised, for this publisher, different topic has different message (both length and content)
                for j in range(10):
                    Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
     
                yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, timestamp)
                # The timestamp for each new message is drifted some seconds (60 + topic id)
                timestamp = timestamp + span;
                count = count + 1
                if (count > 0 and count % 100 == 0):
                    print("Published " + str(count) + " messages at " + str(datetime.datetime.now()))
            yield Publisher.Close()
    
    except Exception, e:
        print "Error: " + str(e)
        Exit_Code = -1
    finally:
        reactor.callFromThread(reactor.callLater, 1, reactor.stop)

@defer.inlineCallbacks
def Run_subscribing_loop(timestamp):
    global Exit_Code;
    global Running;
    try :
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open(Synapse_host, Synapse_port)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"
        start_time = 1470120000000000;
        if (timestamp != 0):
            start_time = timestamp * 1000000;
        yield Subscriber.Subscribe("test.topic", Synapse_subscription_options(Begin_utc = start_time, Supply_metadata = True))
        sequence_num = 0
        timestamp = 0
        span = 1000000;
        while (True) :
            global Exit_Code
            Message_envelope = yield Subscriber.Next(30)
            if (Message_envelope == None):
                print("Waiting message timeout.");
                Exit_Code = -1;
                break;
            num = Message_envelope.Amqp.Decode_sequence_number();
            sequence_num = sequence_num + 1;
            msg_time = Message_envelope.Amqp.Decode_timestamp();
            # To check timestamp of the message is following our rule
            if (timestamp == 0):
                if (msg_time > start_time):
                    print("Expected timestamp " + str(start_time) + " is not received.");
                    Exit_Code = -1;
                    break;
                elif (msg_time < start_time):
                    continue;
                else:
                    if (Exit_Code != -1):
                        Exit_Code = 0
                    break;


            if (sequence_num % 100 == 0):
                print(str(id) + ": Total messages received is " + str(sequence_num))


        print("Sequence number for the last record is " + str(sequence_num))

    except Exception, e:
        print "Error: " + str(e)
        Exit_Code = -1
    finally:
        reactor.callFromThread(reactor.callLater, 1, reactor.stop)

@defer.inlineCallbacks
def SubscribeMsgs(timestamp):
    global Exit_Code;
    global Running;
    try :
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open(Synapse_host, Synapse_port)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"
        start_time = 1470120000000000;
        if (timestamp != 0):
            start_time = timestamp * 1000000;
        yield Subscriber.Subscribe("test.topic", Synapse_subscription_options(Begin_utc = start_time, Supply_metadata = True))
        sequence_num = 0
        timestamp = 0
        span = 1000000;
        while (True) :
            global Exit_Code
            Message_envelope = yield Subscriber.Next(30)
            if (Message_envelope == None):
                print("Waiting message timeout.");
                Exit_Code = -1;
                break;
            num = Message_envelope.Amqp.Decode_sequence_number();
            sequence_num = sequence_num + 1;
            msg_time = Message_envelope.Amqp.Decode_timestamp();
            # To check timestamp of the message is following our rule
            if (timestamp == 0):
                if (msg_time > start_time):
                    print("Expected timestamp " + str(start_time) + " is not received.");
                    Exit_Code = -1;
                    break;
                else:
                    timestamp = msg_time;
            else:
                if (msg_time - timestamp != span):
                    print("Message is not received as published sequence.")
                    Exit_Code = -1;
                    break;
                else:
                    if msg_time == 1470239999000000:
                        break;
                    else:
                        timestamp = msg_time;

            if (sequence_num % 100 == 0):
                print("Total messages received is " + str(sequence_num))
        print("Sequence number for the last record is " + str(sequence_num))

    except Exception, e:
        print "Error: " + str(e)
        Exit_Code = -1
    finally:
        reactor.callFromThread(reactor.callLater, 1, reactor.stop)

@defer.inlineCallbacks
def TruncateTopic(timestamp):
    global Running;
    global Exit_Code;
    try : 
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type())
        yield Subscriber.Open(Synapse_host, Synapse_port, 15)
        trunc_time = 1470120000000000
        if (timestamp != 0):
            trunc_time = timestamp * 1000000;
        yield Subscriber._DONT_USE_JUST_YET_Set_topic_sparsification("test.topic", 0, trunc_time)
        yield Subscriber.Close();
    except Exception, e :
        print("Error: " + str(e));
        Exit_Code = -1
    finally:
        reactor.callFromThread(reactor.callLater, 1, reactor.stop)

if __name__ == '__main__':
    Running = 1;
    timestamp = 0;
    if (len(sys.argv) >= 3):
        timestamp = int(sys.argv[2]);
    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "Pub"):
            reactor.callFromThread(PublishMsgs, timestamp)
        elif sys.argv[1] == "Sub":
            reactor.callFromThread(Run_subscribing_loop, timestamp);
        elif sys.argv[1] == "Trun":
            reactor.callFromThread(TruncateTopic, timestamp);
        elif sys.argv[1] == "SubAll":
            reactor.callFromThread(SubscribeMsgs, timestamp);
        else:
            reactor.callFromThread(PublishMsgs, timestamp);
    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);
