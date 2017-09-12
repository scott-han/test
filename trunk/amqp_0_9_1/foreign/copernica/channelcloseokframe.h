/**
 *  Class describing a channel close acknowledgement frame
 *
 *  @copyright 2014 Copernica BV
 */

#include "channelframe.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNEL_CLOSE_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNEL_CLOSE_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ChannelCloseOKFrame final : public ChannelFrame
{
protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        ChannelFrame::fill(buffer);
    }

public:
    /**
     *  Construct a channel close ok  frame
     *  @param  frame
     */
    ChannelCloseOKFrame(ReceivedFrame &frame) :
        ChannelFrame(frame)
    {}

    /**
     *  Construct a channel close ok  frame
     *
     *  @param  channel     channel we're working on
     */
    ChannelCloseOKFrame(uint16_t channel) :
        ChannelFrame(channel, 0)
    {}

    /**
     *  Destructor
     */
    virtual ~ChannelCloseOKFrame() = default;

    /**
     *  Method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 41;
    }

    /**
     *  Process the frame
     *  @param  connection      The connection over which it was received
     *  @return bool            Was it succesfully processed?
     */
    virtual bool process(ConnectionImpl *) override
    {
#if 0
        // we need the appropriate channel
        auto channel = connection->channel(this->channel());

        // channel does not exist
        if (!channel) return false;

        // report that the channel is closed
        if (channel->reportClosed()) channel->onSynchronized();

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
