#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_MESSAGE_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_MESSAGE_H
/**
 *  Message.h
 *
 *  An incoming message has the same sort of information as an outgoing
 *  message, plus some additional information.
 *
 *  Message objects can not be constructed by end users, they are only constructed
 *  by the AMQP library, and passed to the ChannelHandler::onDelivered() method
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
class Message : public Envelope
{
protected:
    /**
     *  The exchange to which it was originally published
     *  @var    string
     */
    std::string _exchange;
    
    /**
     *  The routing key that was originally used
     *  @var    string
     */
    std::string _routingKey;
    
protected:
    /**
     *  The constructor is protected to ensure that endusers can not
     *  instantiate a message
     *  @param  exchange
     *  @param  routingKey
     */
    Message(const std::string &exchange, const std::string &routingKey) :
        Envelope(nullptr, 0), _exchange(exchange), _routingKey(routingKey)
    {}
    
public:
    /**
     *  Destructor
     */
    virtual ~Message() {}

    /**
     *  The exchange to which it was originally published
     *  @var    string
     */
    const std::string &exchange() const
    {
        return _exchange;
    }
    
    /**
     *  The routing key that was originally used
     *  @var    string
     */
    const std::string &routingKey() const
    {
        return _routingKey;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
