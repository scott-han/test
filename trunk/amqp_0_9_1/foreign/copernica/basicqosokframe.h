/**
 *  Class describing a basic QOS frame
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
class BasicQosOKFrame final : public BasicFrame
{
protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param      buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base, then done (no other params)
        BasicFrame::fill(buffer);
    }

public:
    /**
     *  Construct a basic qos ok frame
     *  @param  channel     channel we're working on
     */
    BasicQosOKFrame(uint16_t channel) : BasicFrame(channel, 0) {}

    /**
     *  Constructor based on incoming data
     *  @param  frame
     */
    BasicQosOKFrame(ReceivedFrame &frame) : BasicFrame(frame) {}

    /**
     *  Destructor
     */
    virtual ~BasicQosOKFrame() = default;

    /**
     *  Return the method id
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
        if (!channel) return false;

        // report
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


