import sys
import traceback
import random
import threading
import time
import logging
import logging.handlers
import datetime
import random
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

#logger = logging.getLogger('pika');

#log_path = os.path.join(os.getcwd(), 'logs', 'test_extend_ack.log');
#handler = logging.handlers.TimedRotatingFileHandler(log_path, 'midnight', 1);
#formatter = logging.Formatter('%(asctime)s %(levelname)s %(message)s');
#handler.setFormatter(formatter);
#logger.addHandler(handler);
#logger.setLevel(logging.DEBUG);
Synapse_host = "localhost"
Synapse_port = 5672
Total_Message = 3000
Total_Loop = 1
Exit_Code = 0
Running = 0
# Below timeout decorator code for defer function is from here: https://gist.github.com/theduderog/735556
def Timeout_wrapper(secs):
    """
    Decorator to add timeout to Deferred calls
    """
    def wrap(func):
        @defer.inlineCallbacks
        def _timeout(*args, **kwargs):
            rawD = func(*args, **kwargs)
            if not isinstance(rawD, defer.Deferred):
                defer.returnValue(rawD)

            timeoutD = defer.Deferred()
            timesUp = reactor.callLater(secs, timeoutD.callback, None)

            try:
                rawResult, timeoutResult = yield defer.DeferredList([rawD, timeoutD], fireOnOneCallback=True, fireOnOneErrback=True, consumeErrors=True)
            except defer.FirstError, e:
                #Only rawD should raise an exception
                assert e.index == 0
                timesUp.cancel()
                e.subFailure.raiseException()
            else:
                #Timeout
                if timeoutD.called:
                    rawD.cancel();
                    print("%s secs have expired since extend_ack subscription started" % secs);
                    if reactor.running:
                        reactor.stop();

            #No timeout
            timesUp.cancel()
            defer.returnValue(rawResult)
        return _timeout
    return wrap

@defer.inlineCallbacks
def Truncate_topic(topic, size, timestamp):
    global Running;
    global Exit_Code;
    try : 
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Message_factory_type());
        yield Subscriber.Open(Synapse_host, Synapse_port, 15);
        trunc_time = 1470120000000000;
        if (timestamp != 0):
            trunc_time = timestamp * 1000000;
        print("Truncate topic " + topic + " at " + str(trunc_time))

        yield Subscriber._DONT_USE_JUST_YET_Set_topic_sparsification(topic, size, trunc_time);
        yield Subscriber.Close();
    except Exception, e :
        print("Error: " + str(e));
        Exit_Code = -1;
    finally:
        if (not Exit_Code == 0 ) and reactor.running:
            reactor.stop();

@defer.inlineCallbacks
def Publish_msgs(topic, id, repeat = 10):
    count = 0
    global Running
    global Exit_Code
    try:
        Publisher = Synapse_publisher()
        yield Publisher.Open(Synapse_host, Synapse_port)
        Topic_name = topic;
        span = 1000000;
        start = 1470000000;
        timestamp = start * 1000000;
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
            timestamp += span;
            count = count + 1
            if (count % 100 == 0):
                print("Published " + str(count) + " messages to topic: " + topic)
            if (count % 20000 == 0):
                # Truncate the topic every 20000 messages
                truncate_at=random.randint(count - 10000, count);
                truncate_at=start + truncate_at;
                yield (Timeout_wrapper(30)(Truncate_topic))(Topic_name, 0, truncate_at);
        yield Publisher.Close()
            #time.sleep(60);
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        Running = Running - 1
        if ((Running == 0) or (not Exit_Code == 0)) and reactor.running:
            reactor.stop()
Extend_ack_in_progress = False;
@defer.inlineCallbacks
def Subscribe_extend_ack(sub, props):
    global Exit_Code;
    try:
        global Extend_ack_in_progress;
        Extend_ack_in_progress = True
        #print("sub.Subscribe_extend_ack starts");
        yield sub.Subscribe_extended_ack(props);
        Extend_ack_in_progress = False
        #print("sub.Subscribe_extend_ack returns")
    except Exception, e :
        print "Error in Subscribe_extend_ack: " + str(e)
        Exit_Code = -1
    finally :
        if (not Exit_Code == 0) and reactor.running:
            reactor.stop()
@defer.inlineCallbacks
def Run_extend_ack_loop(topic, fullmode = True):
    global Running
    global Exit_Code
    global Extend_ack_in_progress;
    try :
        sub = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory());
        yield sub.Open(Synapse_host, Synapse_port);
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse";
        version = sub.Client.server_properties['version'];
        info = sub.Client.server_properties['information'];
        print("Synapse information:'"+info+"', version:'" + version + "'");
        props = {};
        props["Topics"] = 1;
        if (FullMode):
            print("Subscribe to full mode as well as extend_ack mode");
            yield sub.Subscribe(topic, Synapse_subscription_options(Begin_utc = 1, Supply_metadata = True));
        else:
            print("Subscribe to extend_ack mode only");
        # in this test, we just keep querying the topic related information from server
        yield sub.Subscribe_extended_ack(props);
        count = 0;
        fmcount = 0;
        while True:
            msg = yield sub.Next(30);
            if (msg is None):
                raise RuntimeError("Could not query topics");
                break;
            if (msg.Is_ack):
                if (msg.Amqp.Properties.headers.has_key("Stats")):
                    array = msg.Amqp.Properties.headers['Stats'];
                    if (count % 1000 == 0): print("Stats:" + str(array));
                if (msg.Amqp.Properties.headers.has_key("Topics")):
                    topics = msg.Amqp.Properties.headers["Topics"];
                    if (count % 1000 == 0): print("Topics:" + str(topics));
                count += 1;
                # in this test, we just keep querying the topic related information from server (Not yield here)
                if not Extend_ack_in_progress: 
                    if (not FullMode): yield (Timeout_wrapper(60)(Subscribe_extend_ack))(sub, props);
                    else: (Timeout_wrapper(60)(Subscribe_extend_ack))(sub, props);
                else:
                    print("Extend_ack_in_progress ...")
            else:
                fmcount += 1;
                if (fmcount % 10000 == 0): 
                    print("Received " + str(fmcount) + " messages(non-ack) in extend_ack subscription");
                    
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        if reactor.running:
            reactor.stop()

@defer.inlineCallbacks
def Run_subscribing_loop(topic, total_messages):
    global Running
    global Exit_Code
    try :
        Subscriber = Synapse_subscriber(Basic_waypoints_factory(), Basic_traceRoute_factory())
        yield Subscriber.Open(Synapse_host, Synapse_port)
        print "[" + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + "] Opened Synapse"

        yield Subscriber.Subscribe(topic, Synapse_subscription_options(Begin_utc = 1, Supply_metadata = True))
        count = 0;
        while (True) :
            Message_envelope = yield Subscriber.Next(30)
            if Message_envelope is None:
              raise RuntimeError("Subscriber.Next() timeout unexpected")
            count += 1;
            if (count % 1000 == 0):
                print("Received " + str(count) + " messages, topic=" + topic);
            if (count >= total_messages):
                print("All messages are received for topic: " + topic);
                break;
    except Exception, e :
        print "Error: " + str(e)
        Exit_Code = -1
    finally :
        Running -= 1;
        if ((Running == 0) or (not Exit_Code == 0)) and reactor.running:
            reactor.stop()

if __name__ == '__main__':
    Total_Topic = 1
    Run_As = 1 # 1=Publish; 2=Subscription; 3=Extend ack
    Start_from = 0;
    if (len(sys.argv) >= 2):
        if (sys.argv[1] == "Sub"):
            Run_As = 2
        elif (sys.argv[1] == "Ext"):
            Run_As = 3;
    if (len(sys.argv) >= 3):
        Total_Message = int(sys.argv[2]);
    if (len(sys.argv) >= 4):
        Total_Topic = int(sys.argv[3]);
    if (len(sys.argv) >= 5):
        Start_from = int(sys.argv[4]);
    Running = Total_Topic;
    FullMode = True;
    if (Total_Topic == 0):
        FullMode = False;

    if (Run_As == 1):
        for m in range(Total_Topic):
            reactor.callFromThread(Publish_msgs, "test.topic" + str(Start_from + m + 1), m, (m % 10) + 1);
    elif (Run_As == 2):
        for m in range(Total_Topic):
            reactor.callFromThread(Run_subscribing_loop, "test.topic" + str(Start_from + m + 1), Total_Message);
    elif (Run_As == 3):
        reactor.callWhenRunning(Run_extend_ack_loop, "test.topic" + str(Total_Message) + "*", FullMode);
    reactor.run()
    print("Task is completed.")
    sys.exit(Exit_Code);