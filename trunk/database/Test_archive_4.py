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
Days_ago = 0
Seq = 1
Stream_id = None
Total_Topic = 1

sleep_time = Total_Time / Total_Message
@defer.inlineCallbacks
def PublishMsgs(topic, id, repeat = 10, daysAgo = 0, seq = 1):
    count = 0
    global Running
    global Exit_Code
    try:
        for k in range(1):
            Publisher = Synapse_publisher()
            yield Publisher.Open(Synapse_host, Synapse_port)
            current = datetime.datetime.utcnow();
            Topic_name = topic
            timestamp = Datetime_to_synapse_timestamp(current);
            timestamp = timestamp - daysAgo * 24 * 60 * 60 * 1000000
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
                yield Publisher.Publish(Topic_name, Contests, None, False, None, seq, timestamp)
                # The timestamp for each new message is drifted some seconds (60 + topic id)
                timestamp = timestamp + (60 + id) * 1000000;
                if (i % 100 == 0):
                    print(str(id) + ": Published " + str(i) + " messages at " + str(datetime.datetime.now()))
            yield Publisher.Close()
            #time.sleep(60);
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        Running = Running - 1;
        if Running == 0 and reactor.running:
            reactor.stop()


if __name__ == '__main__':
    Total_Topic = 1
    Run_As_Publisher = True
    SubStrict = False
    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "Sub"):
            Run_As_Publisher = False
        elif (sys.argv[1] == "SubStrict"): #No prefixed any info for the topic name.
            SubStrict = True
            Run_As_Publisher = False
    if (len(sys.argv) >= 3):
        Total_Message = int(sys.argv[2]);
    if (len(sys.argv) >= 4):
        Total_Topic = int(sys.argv[3]);
    if (len(sys.argv) >= 5):
        Topic = sys.argv[4];
    if (len(sys.argv) >= 6):
        Days_ago = int(sys.argv[5]);
    if (len(sys.argv) >= 7):
        Seq = int(sys.argv[6]);
    Running = Total_Topic
    if (Run_As_Publisher):
        for m in range(Total_Topic):
            reactor.callFromThread(PublishMsgs, "test." + Topic + str(m + 1), m, 10, Days_ago, Seq)

    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);
