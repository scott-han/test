/**
 *  Class describing an AMQP queue unbind ok frame
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Class definition
 */


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_QUEUE_UNBIND_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_QUEUE_UNBIND_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class QueueUnbindOKFrame final : public QueueFrame
{
protected:
    /**
     *  Encode a queueunbindokframe on a stringbuffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        QueueFrame::fill(buffer);
    }
public:
    /**
     * Decode a queueunbindokframe from a received frame
     *
     * @param   frame   received frame to decode
     * @return  shared pointer to created frame
     */
    QueueUnbindOKFrame(ReceivedFrame& frame) :
        QueueFrame(frame)
    {}

    /**
     * construct a queueunbindokframe
     *
     * @param   channel     channel identifier
     */
    QueueUnbindOKFrame(uint16_t channel) :
        QueueFrame(channel, 0)
    {}

    /**
     *  Destructor
     */
    virtual ~QueueUnbindOKFrame() = default;

    /**
     * returns the method id
     * @return  uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 51;
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

        // report queue unbind success
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
