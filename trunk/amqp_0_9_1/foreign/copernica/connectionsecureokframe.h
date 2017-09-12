/**
 *  Class describing connection setup security challenge response
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
class ConnectionSecureOKFrame final : public ConnectionFrame
{
private:
    /**
     *  The security challenge response
     *  @var LongString
     */
    LongString _response;

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

        // add fields
        _response.fill(buffer);
    }

public:
    /**
     *  Construct a connection security challenge response frame
     *
     *  @param  response    the challenge response
     */
    ConnectionSecureOKFrame(const std::string& response) :
        ConnectionFrame(response.length() + 4), //response length + uint32_t for encoding the length
        _response(response)
    {}

    /**
     *  Construct a connection security challenge response frame from a received frame
     *
     *  @param  frame   received frame
     */
    ConnectionSecureOKFrame(ReceivedFrame &frame) :
        ConnectionFrame(frame),
        _response(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~ConnectionSecureOKFrame() = default;

    /**
     *  Method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 21;
    }

    /**
     *  Get the challenge response
     *  @return string
     */
    const std::string& response() const
    {
        return _response;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


