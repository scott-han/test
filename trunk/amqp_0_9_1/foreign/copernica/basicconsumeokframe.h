/**
 *  Class describing a basic consume ok frame
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...
//
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_CONSUME_OK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_CONSUME_OK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class BasicConsumeOKFrame final : public BasicFrame
{
private:
    /**
     *  Holds the consumer tag specified by the client or provided by the server.
     *  @var ShortString
     */
    ShortString _consumerTag;

protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        BasicFrame::fill(buffer);

        // add payload
        _consumerTag.fill(buffer);
    }

public:
    /**
     *  Construct a basic consume frame
     *
     *  @param  consumerTag       consumertag specified by client of provided by server
     */
    BasicConsumeOKFrame(uint16_t channel, const std::string& consumerTag) :
        BasicFrame(channel, consumerTag.length() + 1), // length of string + 1 for encoding of stringsize
        _consumerTag(consumerTag)
    {}

    /**
     *  Construct a basic consume ok frame from a received frame
     *
     *  @param frame    received frame
     */
    BasicConsumeOKFrame(ReceivedFrame &frame) :
        BasicFrame(frame),
        _consumerTag(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~BasicConsumeOKFrame() = default;

    /**
     *  Return the method ID
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 21;
    }

    /**
     *  Return the consumertag, which is specified by the client or provided by the server
     *  @return std::string
     */
    const std::string& consumerTag() const
    {
        return _consumerTag;
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
        if (channel->reportSuccess(consumerTag())) channel->onSynchronized();

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
