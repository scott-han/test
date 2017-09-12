/**
 *  Class describing an AMQP exchange delete ok frame
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ExchangeDeleteOKFrame : public ExchangeFrame
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
     *  Construct an exchange delete ok frame
     *
     *  @param  frame   received frame
     */
    ExchangeDeleteOKFrame(ReceivedFrame &frame) :
        ExchangeFrame(frame)
    {}

    /**
     *  Construct an exchange delete ok frame
     *
     *  @param  channel     channel we're working on
     */
    ExchangeDeleteOKFrame(uint16_t channel) : ExchangeFrame(channel, 0) {}

    /**
     *  Destructor
     */
    virtual ~ExchangeDeleteOKFrame() {}

    /**
     *  returns the method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 21;
    }

    /**
     *  Process the frame
     *  @param  connection      The connection over which it was received
     *  @return bool            Was it succesfully processed?
     */
    virtual bool process(ConnectionImpl *connection) override
    {
        // check if we have a channel
        auto channel = connection->channel(this->channel());

        // channel does not exist
        if(!channel) return false;

        // report to handler
        if (channel->reportSuccess()) channel->onSynchronized();

        // done
        return true;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


