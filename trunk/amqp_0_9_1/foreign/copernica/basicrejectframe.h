/**
 *  Class describing a basic reject frame
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
 *  Class definition
 */
class BasicRejectFrame final : public BasicFrame 
{
private:
    /**
     *  server-assigned and channel specific delivery tag
     *  @var int64_t
     */
    int64_t _deliveryTag;

    /**
     *  Server will try to requeue messages. If requeue is false or requeue attempt fails, messages are discarded or dead-lettered
     *  @var BooleanSet
     */
    BooleanSet _requeue;

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

        // encode fields
        buffer.add(_deliveryTag);
        _requeue.fill(buffer);
    }

public:
    /**
     *  construct a basic reject frame from a received frame
     *
     *  @param  frame   received frame
     */
    BasicRejectFrame(ReceivedFrame &frame) :
        BasicFrame(frame),
        _deliveryTag(frame.nextInt64()),
        _requeue(frame)
    {}

    /**
     *  Construct a basic reject frame
     *
     *  @param  channel         channel id
     *  @param  deliveryTag     server-assigned and channel specific delivery tag
     *  @param  requeue         whether to attempt to requeue messages
     */
    BasicRejectFrame(uint16_t channel, int64_t deliveryTag, bool requeue = true) :
        BasicFrame(channel, 9), // 8 for uint64_t, 1 for bool
        _deliveryTag(deliveryTag),
        _requeue(requeue)
    {}

    /**
     *  Destructor
     */ 
    virtual ~BasicRejectFrame() = default;

    /**
     *  Is this a synchronous frame?
     *
     *  After a synchronous frame no more frames may be
     *  sent until the accompanying -ok frame arrives
     */
    bool synchronous() const override
    {
        return false;
    }

    /**
     * Return the method ID
     * @return  uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 90;
    }

    /**
     *  Return the server-assigned and channel specific delivery tag
     *  @return  uint64_t
     */
    int64_t deliveryTag() const
    {
        return _deliveryTag;
    }

    /**
     *  Return whether the server will try to requeue
     *  @return bool
     */
     bool requeue() const
     {
        return _requeue.get(0);
     }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


