/**
 *  Class describing connection vhost open acknowledgement frame
 * 
 *  Message sent by the server to the client to confirm that a connection to
 *  a vhost could be established
 * 
 *  @copyright 2014 Copernica BV
 */

#include "connectionframe.h"


// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONOPENOK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONOPENOK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ConnectionOpenOKFrame final : public ConnectionFrame
{
private:
    /**
     *  Deprecated field we need to read
     *  @var ShortString
     */
    ShortString _deprecatedKnownHosts;

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

        // add deprecaed field
        _deprecatedKnownHosts.fill(buffer);
    }
public:
    /**
     *  Construct a connectionopenokframe from a received frame
     *
     *  @param  frame   received frame
     */
    ConnectionOpenOKFrame(ReceivedFrame &frame) :
        ConnectionFrame(frame),
        _deprecatedKnownHosts(frame)
    {}

    /**
     *  Construct a connectionopenokframe
     *
     */
    ConnectionOpenOKFrame() :
        ConnectionFrame(1), // for the deprecated shortstring
        _deprecatedKnownHosts("")
    {}

    /**
     *  Destructor
     */
    virtual ~ConnectionOpenOKFrame() = default;

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
        // all is ok, mark the connection as connected
        connection->setConnected();
        
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

