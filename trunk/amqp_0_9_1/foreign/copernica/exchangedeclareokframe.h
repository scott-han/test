/**
 *  Class describing an AMQP exchange declare ok frame
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Class definition
 */


// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCHANGE_DECLARE_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCHANGE_DECLARE_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 * Class implementation
 */
class ExchangeDeclareOKFrame final : public ExchangeFrame
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
        ExchangeFrame::fill(buffer);
    }
public:
    /**
     *  Construct an exchange declare ok frame
     *
     *  @param  channel     channel we're working on
     */
    ExchangeDeclareOKFrame(uint16_t channel) : ExchangeFrame(channel, 0) {}

    /**
     *  Decode an exchange declare acknowledgement frame
     *
     *  @param  frame   received frame to decode
     */
    ExchangeDeclareOKFrame(ReceivedFrame &frame) :
        ExchangeFrame(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~ExchangeDeclareOKFrame() = default;

    /**
     *  returns the method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 11;
    }

    /**
     *  Process the frame
     *  @param  connection      The connection over which it was received
     *  @return bool            Was it succesfully processed?
     */
    virtual bool process(ConnectionImpl *) override
    {
#if 0
        // we need the appropriate channel
        auto channel = connection->channel(this->channel());

        // channel does not exist
        if(!channel) return false;

        // report exchange declare ok
        if (channel->reportSuccess()) channel->onSynchronized();

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
