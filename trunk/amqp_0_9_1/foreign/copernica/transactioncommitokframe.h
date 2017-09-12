/**
 *  Class describing an AMQP transaction commit ok frame
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
class TransactionCommitOKFrame final : public TransactionFrame
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
        TransactionFrame::fill(buffer);
    }

public:
    /**
     *  Construct a transaction commit ok frame
     * 
     *  @param   channel     channel identifier
     *  @return  newly created transaction commit ok frame
     */
    TransactionCommitOKFrame(uint16_t channel) :
        TransactionFrame(channel, 0)
    {}

    /**
     *  Constructor on incoming data
     *
     *  @param   frame   received frame to decode
     */
    TransactionCommitOKFrame(ReceivedFrame& frame) :
        TransactionFrame(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~TransactionCommitOKFrame() = default;

    /**
     *  return the method id
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
        // we need the appropriate channel
        auto channel = connection->channel(this->channel());

        // channel does not exist
        if(!channel) return false;

        // report that the channel is open
        if (channel->reportSuccess()) channel->onSynchronized();

        // done
        return true;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


