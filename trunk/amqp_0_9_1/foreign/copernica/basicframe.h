/**
 *  Class describing an AMQP basic frame
 * 
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


#include "methodframe.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 * Class implementation
 */
class BasicFrame : public MethodFrame
{
protected:
    /**
     *  Constructor
     *  @param  channel     The channel ID
     *  @param  size        Payload size
     */
    BasicFrame(uint16_t channel, uint32_t size) : MethodFrame(channel, size) {}

    /**
     *  Constructor based on a received frame
     *  @param  frame
     */
    BasicFrame(ReceivedFrame &frame) : MethodFrame(frame) {}


public:
    /**
     *  Destructor
     */
    virtual ~BasicFrame() = default;

    /** 
     *  Class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 60;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
