/**
 *  Class describing a basic cancel ok frame
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_CANCEL_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_CANCEL_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class BasicCancelOKFrame final : public BasicFrame
{
private:
    /**
     *  Holds the consumer tag specified by the client or provided by the server.
     *  @var ShortString
     */
    ShortString _consumerTag;

protected:
    /**
     * Encode a frame on a string buffer
     *
     * @param   buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        BasicFrame::fill(buffer);

        // add own information
        _consumerTag.fill(buffer);
    }

public:
    /**
     *  Construct a basic cancel ok frame
     *
     *  @param  frame   received frame
     */
    BasicCancelOKFrame(ReceivedFrame &frame) :
        BasicFrame(frame),
        _consumerTag(frame)
    {}

    /**
     *  Construct a basic cancel ok frame (client-side)
     *  @param  channel     channel identifier
     *  @param  consumerTag holds the consumertag specified by client or server
     */
    BasicCancelOKFrame(uint16_t channel, ::std::string const & consumerTag) :
        BasicFrame(channel, consumerTag.length() + 1),    // add 1 byte for encoding the size of consumer tag
        _consumerTag(consumerTag)
    {}

    /**
     *  Destructor
     */
    virtual ~BasicCancelOKFrame() = default;

    /**
     *  Return the consumertag, which is specified by the client or provided by the server
     *  @return string
     */
    const std::string& consumerTag() const
    {
        return _consumerTag;
    }

    /**
     *  Return the method ID
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 31;
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
        if (channel->reportSuccess<const std::string&>(consumerTag())) channel->onSynchronized();

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
