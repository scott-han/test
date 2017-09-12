/**
 *  Base class for a message implementation
 *
 *  @copyright 2014 Copernica BV
 */

// some contents modified by leon zadorin ...

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class definition
 */
class ConsumedMessage : public MessageImpl
{
private:
    /**
     *  The consumer tag
     *  @var string
     */
    std::string _consumerTag;

    /**
     *  The delivery tag
     *  @var uint64_t
     */
    uint64_t _deliveryTag;

    /**
     *  Is this a redelivered message?
     *  @var bool
     */
    bool _redelivered;


public:
    /**
     *  Constructor
     *  @param  frame
     */
    ConsumedMessage(const BasicDeliverFrame &frame) :
        MessageImpl(frame.exchange(), frame.routingKey()),
        _consumerTag(frame.consumerTag()), _deliveryTag(frame.deliveryTag()), _redelivered(frame.redelivered())
    {}

    /**
     *  Constructor
     *  @param  frame
     */
    ConsumedMessage(const BasicGetOKFrame &frame) :
        MessageImpl(frame.exchange(), frame.routingKey()),
        _deliveryTag(frame.deliveryTag()), _redelivered(frame.redelivered())
    {}


    /**
     *  Destructor
     */
    virtual ~ConsumedMessage() {}

    /**
     *  Retrieve the consumer tag
     *  @return std::string
     */
    const std::string &consumer() const
    {
        return _consumerTag;
    }
     
    /**
     *  Report to the handler
     *  @param  callback
     */
    void report(const MessageCallback &callback) const
    {
        // send ourselves to the consumer
        if (callback) callback(*this, _deliveryTag, _redelivered);
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

