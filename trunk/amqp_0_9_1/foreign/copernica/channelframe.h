/**
 *  Class describing an AMQP channel frame
 * 
 *  @copyright 2014 Copernica BV
 */

#include "methodframe.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNEL_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNEL_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ChannelFrame : public MethodFrame
{
protected:
    /**
     *  Constructor for a channelFrame
     *  
     *  @param  channel     channel we're working on
     *  @param  size        size of the frame
     */
    ChannelFrame(uint16_t channel, uint32_t size) : MethodFrame(channel, size) {}

    /**
     *  Constructor that parses an incoming frame
     *  @param  frame       The received frame
     */
    ChannelFrame(ReceivedFrame &frame) : MethodFrame(frame) {}

public:
    /**
     *  Destructor
     */
    virtual ~ChannelFrame() = default; 

    /**
     *  Class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 20;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
