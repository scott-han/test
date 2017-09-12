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
def PublishMsgs(topic, id, repeat = 10, start = 0, mode = 0):
    count = 0
    global Running
    global Exit_Code
    try:
        for k in range(1):
            Publisher = Synapse_publisher()
            yield Publisher.Open(Synapse_host, Synapse_port)
            Topic_name = topic;
            timestamp = start;
            span = 1000000;
            if (start == 0):
                current = datetime.datetime.now();
                timestamp = Datetime_to_synapse_timestamp(current);
            else:
                timestamp = start * 1000000;
            if mode == 0:
                timestamp -= 86400 * 15 * 1000000;
            else:
                timestamp -= 86400 * (16 - mode) * 1000000;

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
     
                yield Publisher.Publish(Topic_name, Contests, None, False, None, 0, timestamp)
                # The timestamp for each new message is drifted some seconds (60 + topic id)
                timestamp = timestamp + (id+1) * span;
                count = count + 1
                if (count > 0 and count % 100 == 0):
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

if __name__ == '__main__':
    Total_Topic = 1
    Start_time = 0
    Mode = 0
    if (len(sys.argv) >= 2):
        Total_Message = int(sys.argv[1]);
    if (len(sys.argv) >= 3):
        Total_Topic = int(sys.argv[2]);
    if (len(sys.argv) >= 4):
        Start_time = int(sys.argv[3]);
    if (len(sys.argv) >= 5):
        Mode = int(sys.argv[4])

    Running = Total_Topic
    for m in range(Total_Topic):
        reactor.callFromThread(PublishMsgs, "test.topic" + str(m + 1), m, ((m + 7) % 10) + 1, Start_time, Mode)

    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);
