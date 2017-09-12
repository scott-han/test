/**
 *  Class describing an AMQP queue bind ok frame
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...
//
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_QUEUE_BIND_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_QUEUE_BIND_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class QueueBindOKFrame final : public QueueFrame
{
protected:
    /**
     *  Fill output buffer
     *  @param  buffer
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        QueueFrame::fill(buffer);
    }

public:
    /**
     *  Construct a queuebindokframe
     *
     *  @param  channel     channel identifier
     */
    QueueBindOKFrame(uint16_t channel) : QueueFrame(channel, 0) {}

    /**
     *  Constructor based on incoming data
     *  @param  frame   received frame
     */
    QueueBindOKFrame(ReceivedFrame &frame) :
        QueueFrame(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~QueueBindOKFrame() = default;

    /**
     *  returns the method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 21;
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

        // channel does not exist
        if(!channel) return false;

        // report to handler
        if (channel->reportSuccess()) channel->onSynchronized();

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
