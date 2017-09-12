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
Waypoints_type = Data_processors.Federated_serialisation.Schemas.Waypoints.ttypes.waypoints
Message_factory_type = Data_processors.Federated_serialisation.Schemas.Contests4.ttypes.Basic_message_factory

Synapse_host = "localhost"
Synapse_port = 5672
Total_Message = 3000
# Total sending time in seconds.
Total_Time = 1800.0
Total_Loop = 1
Exit_Code = 0
Running = 0

@defer.inlineCallbacks
def PublishMsgs(topic, total_topic, start_seq = 1, span=0, wp = False):
    count = 0
    global Running
    global Exit_Code
    global Total_Message;

    try:
        for k in range(1):
            Publisher = Synapse_publisher()
            yield Publisher.Open(Synapse_host, Synapse_port)
            Topic_name = topic;
            timestamp = 0;

            for i in range(Total_Message):
                Contests = Contests_type()
                Contest = Contests.Set_contest_element("Some_contest_id_key")
                Contest.Set_startDate(Basic_shared_date(2001, 1, 2))
                Contest.Set_startTime(Basic_shared_time(1, 2, 3, 555))
                Contest.Set_scheduledStart(Basic_shared_timestamp(2001, 1, 3, 1, 2, 3, 555))
                Contest_name = Contest.Set_contestName(Basic_shared_text("Some contest name " + str(i + 1)))
                # Please be advised, for this publisher, different topic has different message (both length and content)
                for j in range(1):
                    Contest_name.Set_value(Contest_name.Get_value() + Contest_name.Get_value())
                waypoints = None;
                if wp:
                    waypoints = Waypoints_type();
                    waypoint = waypoints.Add_path_element();
                    waypoint.Set_tag("Test only");
                    waypoint.Set_messageId("Test messageId");
                    waypoint.Set_contentId("Test contentId" + str(start_seq));
                
     
                yield Publisher.Publish(Topic_name + str(1+(start_seq % total_topic)), Contests, waypoints, False, None, start_seq, timestamp)
                start_seq = start_seq + 1;
                # The timestamp for each new message is drifted some seconds (60 + topic id)
                count = count + 1
                if (span != 0 and count % 10 == 9):
                    time.sleep(span * 1.0 / 1000);
                if (count % 100 == 0):
                    print("Published " + str(count) + " messages at " + str(datetime.datetime.now()))
            yield Publisher.Close()
            #time.sleep(60);
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        if reactor.running:
            reactor.stop()
            
@defer.inlineCallbacks
def Receive_messages(sub, seqs, total, wp, time_out=5, expected_timeout = False):
    count = 0;
    num = 0;
    while (True) :
        Message_envelope = yield sub.Next(time_out)
        if Message_envelope is None:
            print("No more messages from a server.")
            if not expected_timeout:
                raise Exception("Timeout while receiving message")
            break;
        num = Message_envelope.Amqp.Decode_sequence_number();
        if (wp):
            waypoints = Message_envelope.Waypoints;
            FindTag = False;
            for x in waypoints.Get_path():
                if x.Get_tag() == "topic_mirror" and x.Get_messageId() == "mirror_1":
                    msg_seq, msg_time = x.Get_contentId().split('-');
                    #print("seq="+str(msg_seq)+",timestamp="+str(msg_time));
                    FindTag = True;
                    #if int(msg_seq) != num1 or int(msg_time) != Message_envelope1.Amqp.Decode_timestamp():
                    #    raise Exception("Received messages doesn't have same order with original server.")
                    seqs.append(int(msg_seq));
            
            if not FindTag:
                raise Exception("Cannot find new waypoint in message")

        else:
            seqs.append(num);
        count += 1;
        if (count % 100 == 0):
            print("Sequence number is " + str(num))
        if (count >= Total_Message):
            print("All message are received.");
            break;
    print("The sequence number for the last message is: " + str(num));

@defer.inlineCallbacks
def Run_subscribing_loop(topic, start_seq, port, wp = False, realtime = True, expected_timeout = False):
    global Exit_Code
    global Total_Message;
    try :
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open("127.0.0.1", port)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"

        yield Subscriber.Subscribe(topic, Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2016, 1, 1, 0, 0, 0)), Supply_metadata = True))

        Subscriber1 = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber1.Open("127.0.0.1", 5672)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"

        yield Subscriber1.Subscribe(topic, Synapse_subscription_options(Begin_utc = Datetime_to_synapse_timestamp(datetime.datetime(2016, 1, 1, 0, 0, 0)), Supply_metadata = True))
        sequence_num = start_seq
        timestamp = 0
        count = 0;
        diff = -1;
        all_seq = [];
        all_seq1 = [];
        time_out=30;
        if expected_timeout: time_out=5;
        a = Receive_messages(Subscriber, all_seq, Total_Message, wp, time_out, expected_timeout);
        b = Receive_messages(Subscriber1, all_seq1, Total_Message, False, time_out, expected_timeout); 
        yield a;
        yield b;
        """
        If client is subscribing to the mirrored topics in realtime rather than history data, there is no way to ensure
        the order of the messages from both Synapse(source and mirror target) are identical, therefore we will have
        to sort them by sequence number and then compare the result.
        """
        if Realtime or wp:
            all_seq1.sort();
            all_seq.sort();
        if (all_seq1 != all_seq):
            raise Exception("Messages from two servers are different.")
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        if reactor.running:
            reactor.stop()


if __name__ == '__main__':
    Total_Topic = 1
    Run_As_Publisher = True
    Start_seq = 1
    Span = 0;
    Realtime = True;
    Waypoint = False;
    Expect_timeout = False;
    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "Sub"):
            Run_As_Publisher = False
    if (len(sys.argv) >= 3):
        Total_Message = int(sys.argv[2]);
    if (len(sys.argv) >= 4):
        Total_Topic = int(sys.argv[3]);
    if (len(sys.argv) >= 5):
        Start_seq = int(sys.argv[4]);
    if (len(sys.argv) >= 6):
        Span = int(sys.argv[5])
    if (len(sys.argv) >= 7):
        if (sys.argv[6] == "wp"): Waypoint = True;
    if (len(sys.argv) >= 8 and sys.argv[7] == "norealtime"):
        Realtime = False;
    if (len(sys.argv) >= 9):
        Expect_timeout = True;

    if (Run_As_Publisher):
        reactor.callFromThread(PublishMsgs, "test.topic", Total_Topic, Start_seq, Span, Waypoint)
    else:
        reactor.callFromThread(Run_subscribing_loop, "test.topic*", Start_seq, Span, Waypoint, Realtime, Expect_timeout)
    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);