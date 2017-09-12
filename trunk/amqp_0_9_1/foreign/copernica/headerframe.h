/**
 *  Class describing an AMQP header frame
 * 
 *  @copyright 2014 Copernica BV
 */


#include "extframe.h"


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_HEADER_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_HEADER_FRAME_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

	class ReceivedFrame;

/**
 *  Class implementation
 */
class HeaderFrame : public ExtFrame
{
protected:
    /**
     *  Construct a header frame
     *  @param  channel     Channel ID
     *  @param  size        Payload size
     */
    HeaderFrame(uint16_t channel, uint32_t size) : ExtFrame(channel, size + 2) {}  // + size of classID (2bytes)

    /**
     *  Construct based on incoming data
     *  @param  frame       Incoming frame
     */
    HeaderFrame(ReceivedFrame &frame) : ExtFrame(frame) { frame.nextUint16(); }

    /**
     *  Encode a header frame to a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    void fill(OutBuffer& buffer) const override
    {
        // call base
        ExtFrame::fill(buffer);

        // add type
        buffer.add(classID());
    }

public:
    /**
     *  Destructor
     */
    virtual ~HeaderFrame() = default;

    /**
     *  Get the message type
     *  @return uint8_t
     */
    uint8_t type() const final 
    {
        return 2;
    }

    /**
     *  Class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const = 0;
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
