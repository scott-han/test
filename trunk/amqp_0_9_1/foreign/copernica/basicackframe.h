/**
 *  Class describing a basic acknowledgement frame
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
 *  Class defintion
 */
class BasicAckFrame : public BasicFrame {
private:
    /**
     *  server-assigned and channel specific delivery tag
     *  @var    uint64_t
     */
    uint64_t _deliveryTag;

    /**
     *  if set, tag is treated as "up to and including", so client can acknowledge multiple messages with a single method
     *  if not set, refers to single message
     *  @var    BooleanSet
     */
    BooleanSet _multiple;

protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param   buffer  buffer to write frame to
     *  @return  pointer to object to allow for chaining
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        BasicFrame::fill(buffer);

        // add the delivery tag
        buffer.add(_deliveryTag);

        // add the booleans
        _multiple.fill(buffer);
    }

public:
    /**
     *  Construct a basic acknowledgement frame
     *
     *  @param  channel         Channel identifier
     *  @param  deliveryTag     server-assigned and channel specific delivery tag
     *  @param  multiple        acknowledge mutiple messages
     */
    BasicAckFrame(uint16_t channel, uint64_t deliveryTag, bool multiple = false) :
        BasicFrame(channel, 9),
        _deliveryTag(deliveryTag),
        _multiple(multiple) {}

    /**
     *  Construct based on received frame
     *  @param  frame
     */
    BasicAckFrame(ReceivedFrame &frame) :
        BasicFrame(frame),
        _deliveryTag(frame.nextUint64()),
        _multiple(frame) {}

    /**
     *  Destructor
     */
    virtual ~BasicAckFrame() = default;

    /**
     *  Is this a synchronous frame?
     *
     *  After a synchronous frame no more frames may be
     *  sent until the accompanying -ok frame arrives
     */
    virtual bool synchronous() const override
    {
        return false;
    }

    /**
     *  Return the method ID
     *  @return  uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 80;
    }

    /**
     *  Return the server-assigned and channel specific delivery tag
     *  @return  uint64_t
     */
    uint64_t deliveryTag() const
    {
        return _deliveryTag;
    }

    /**
     *  Return whether to acknowledgement multiple messages
     *  @return  bool
     */
    bool multiple()
    {
        return _multiple.get(0);
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


