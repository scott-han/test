/**
 *  Class describing connection close acknowledgement frame
 * 
 *  @copyright 2014 Copernica BV
 */

#include "connectionframe.h"

// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONCLOSEOK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONCLOSEOK_FRAME_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ConnectionCloseOKFrame final : public ConnectionFrame
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
        ConnectionFrame::fill(buffer);
    }
public:
    /**
     *  Constructor based on a received frame
     *
     *  @param frame    received frame
     */
    ConnectionCloseOKFrame(ReceivedFrame &frame) :
        ConnectionFrame(frame)
    {}

    /**
     *  construct a channelcloseokframe object
     */
    ConnectionCloseOKFrame() :
        ConnectionFrame(0)
    {}

    /**
     *  Destructor
     */
    virtual ~ConnectionCloseOKFrame() = default;

    /**
     *  Method id
     */
    virtual uint16_t methodID() const override
    {
        return 51;
    }
    
    /**
     *  Process the frame
     *  @param  connection
     */
    virtual bool process(ConnectionImpl *) override
    {
#if 0
        // report that it is closed
        connection->reportClosed();
        
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
