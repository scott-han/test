/**
 *  Class describing an AMQP queue frame
 * 
 *  @copyright 2014 Copernica BV
 */


#include "methodframe.h"

// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_QUEUE_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_QUEUE_FRAME_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class QueueFrame : public MethodFrame
{
protected:
    /**
     *  Construct a queueframe
     *  @param  channel  channel identifier
     *  @param   size     size of the frame
     */
    QueueFrame(uint16_t channel, uint32_t size) : MethodFrame(channel, size) {}

    /**
     *  Constructor based on incoming data
     *  @param  frame   received frame
     */
    QueueFrame(ReceivedFrame &frame) : MethodFrame(frame) {}

public:
    /**
     *  Destructor
     */
    virtual ~QueueFrame() = default;
    
    /**
     *  returns the class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 50;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
