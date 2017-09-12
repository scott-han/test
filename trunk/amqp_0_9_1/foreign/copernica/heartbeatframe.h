/**
 *  Class describing an AMQP Heartbeat Frame
 * 
 *  @copyright 2014 Copernica BV
 */

#include "extframe.h"

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_HEARTBEATFRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_HEARTBEATFRAME_H


// some contents modified by leon zadorin ...

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */ 
class HeartbeatFrame final : public ExtFrame 
{
public:
    /**
     *  Construct a heartbeat frame
     *
     *  @param  channel     channel identifier
     *  @param  payload     payload of the body
     */
    HeartbeatFrame() :
        ExtFrame(0, 0)
    {}

    /**
     *  Decode a received frame to a frame
     *
     *  @param  frame   received frame to decode
     *  @return shared pointer to newly created frame
     */
    HeartbeatFrame(ReceivedFrame& frame) :
        ExtFrame(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~HeartbeatFrame() = default;

    /**
     *  Return the type of frame
     *  @return     uint8_t
     */ 
    uint8_t type() const override
    {
        // the documentation says 4, rabbitMQ sends 8
        return 8;
    }

    /**
     *  Process the frame
     *  @param  connection      The connection over which it was received
     *  @return bool            Was it succesfully processed?
     */
    virtual bool process(ConnectionImpl *) override
    {
        // send back the same frame
        //connection->send(*this);

        // done
        return true;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
