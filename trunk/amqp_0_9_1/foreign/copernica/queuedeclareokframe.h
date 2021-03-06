/**
 *  Class describing an AMQP queue declare ok frame
 *
 *  @copyright 2014 Copernica BV
 */

#include "queueframe.h"


// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_QUEUE_DECLARE_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_QUEUE_DECLARE_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class QueueDeclareOKFrame final : public QueueFrame
{
private:
    /**
     *  The queue name
     *  @var ShortString
     */
    ShortString _name;

    /**
     *  Number of messages
     *  @var int32_t
     */
    int32_t _messageCount;

    /**
     *  Number of Consumers
     *  @var int32_t
     */
    int32_t _consumerCount;


protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        QueueFrame::fill(buffer);

        // add fields
        _name.fill(buffer);
        buffer.add(_messageCount);
        buffer.add(_consumerCount);
    }

public:
    /**
     *  Construct a channel flow frame
     *
     *  @param  channel         channel identifier
     *  @param  code            reply code
     *  @param  text            reply text
     *  @param  failingClass    failing class id if applicable
     *  @param  failingMethod   failing method id if applicable
     */
    QueueDeclareOKFrame(uint16_t channel, const std::string& name, int32_t messageCount, int32_t consumerCount) :
        QueueFrame(channel, name.length() + 9), // 4 per int, 1 for string size
        _name(name),
        _messageCount(messageCount),
        _consumerCount(consumerCount)
    {}

    /**
     *  Constructor based on incoming data
     *  @param  frame   received frame
     */
    QueueDeclareOKFrame(ReceivedFrame &frame) :
        QueueFrame(frame),
        _name(frame),
        _messageCount(frame.nextInt32()),
        _consumerCount(frame.nextInt32())
    {}

    /**
     *  Destructor
     */
    virtual ~QueueDeclareOKFrame() = default;

    /**
     *  Method id
     */
    virtual uint16_t methodID() const override
    {
        return 11;
    }

    /**
     *  Queue name
     *  @return string
     */
    const std::string& name() const
    {
        return _name;
    }

    /**
     *  Number of messages
     *  @return int32_t
     */
    uint32_t messageCount() const
    {
        return _messageCount;
    }

    /**
     *  Number of consumers
     *  @return int32_t
     */
    uint32_t consumerCount() const
    {
        return _consumerCount;
    }

    /**
     *  Process the frame
     *  @param  connection      The connection over which it was received
     *  @return bool            Was it succesfully processed?
     */
    virtual bool process(ConnectionImpl *) override
    {
#if 0
        // check if we have a channel
        auto channel = connection->channel(this->channel());

        // what if channel doesn't exist?
        if (!channel) return false;

        // report success
        if (channel->reportSuccess(name(), messageCount(), consumerCount())) channel->onSynchronized();

        // done
        return true;
#endif
				return false;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
