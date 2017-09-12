/**
 *  Class describing an AMQP connection frame
 * 
 *  @copyright 2014 Copernica BV
 */


#include "methodframe.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTION_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTION_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ConnectionFrame : public MethodFrame
{
protected:
    /**
     *  Constructor for a connectionFrame
     *  
     *  A connection frame never has a channel identifier, so this is passed
     *  as zero to the base constructor
     * 
     *  @param  size        size of the frame
     */
    ConnectionFrame(uint32_t size) : MethodFrame(0, size) {}

    /**
     *  Constructor based on a received frame
     *  @param  frame
     */
    ConnectionFrame(ReceivedFrame &frame) : MethodFrame(frame) {}

public:
    /**
     *  Destructor
     */
    virtual ~ConnectionFrame() = default;

    /**
     *  Class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 10;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
