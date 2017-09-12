/**
 *  Class describing an AMQP exchange frame
 * 
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCHANGE_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCHANGE_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ExchangeFrame : public MethodFrame
{
protected:
    /**
     *  Constructor based on incoming data
     *
     *  @param  frame   received frame to decode
     */
    ExchangeFrame(ReceivedFrame &frame) : MethodFrame(frame) {}

    /**
     *  Constructor for an exchange frame
     *
     *  @param  channel     channel we're working on
     *  @param  size        size of the payload
     */
    ExchangeFrame(uint16_t channel, uint32_t size) : MethodFrame(channel, size) {}

public:
    /**
     *  Destructor
     */
    virtual ~ExchangeFrame() = default;

    /**
     *  Class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 40;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
