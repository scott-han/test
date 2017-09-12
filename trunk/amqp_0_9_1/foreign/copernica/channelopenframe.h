/**
 *  Class describing a channel open frame
 * 
 *  @copyright 2014 Copernica BV
 */

#include "channelframe.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNELOPEN_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNELOPEN_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ChannelOpenFrame final : public ChannelFrame
{
protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    void fill(OutBuffer& buffer) const override
    {
        // call base
        ChannelFrame::fill(buffer);
        
        // add deprecated data
        ShortString unused;
        
        // add to the buffer
        unused.fill(buffer);
    }

public:
    /**
     *  Constructor to create a channelOpenFrame
     *
     *  @param  channel     channel we're working on
     */
    ChannelOpenFrame(uint16_t channel) : ChannelFrame(channel, 1) {}    // 1 for the deprecated shortstring size

    /**
     *  Construct to parse a received frame
     *  @param  frame
     */
    ChannelOpenFrame(ReceivedFrame &frame) : ChannelFrame(frame)
    {
        // deprecated argument
        ShortString unused(frame);
    }

    /**
     *  Destructor
     */
    virtual ~ChannelOpenFrame() = default;

    /**
     *  Method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 10;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
