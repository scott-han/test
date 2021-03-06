/**
 *  Envelope.h
 *
 *  When you send or receive a message to the rabbitMQ server, it is encapsulated
 *  in an envelope that contains additional meta information as well.
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_ENVELOPE_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_ENVELOPE_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class definition
 */
class Envelope final : public MetaData
{
protected:
    /**
     *  The body (only used when string object was passed to constructor
     *  @var    std::string
     */
    std::string _str;

    /**
     *  Pointer to the body data (the memory buffer is not managed by the AMQP
     *  library!)
     *  @var    const char *
     */
    const char *_body;
    
    /**
     *  Size of the data
     *  @var    uint64_t
     */
    uint64_t _bodySize;

public:
    /**
     *  Constructor
     * 
     *  The data buffer that you pass to this constructor must be valid during
     *  the lifetime of the Envelope object.
     * 
     *  @param  body
     *  @param  size
     */
    Envelope(const char *body, uint64_t size) : MetaData(), _body(body), _bodySize(size) {}
    
    /**
     *  Constructor based on a string
     *  @param  body
     */
    Envelope(const std::string &body) : MetaData(), _str(body), _body(_str.data()), _bodySize(_str.size()) {}

    /**
     *  Destructor
     */
    virtual ~Envelope() = default;
    
    /**
     *  Access to the full message data
     *  @return buffer
     */
    const char *body() const
    {
        return _body;
    }
    
    /**
     *  Size of the body
     *  @return uint64_t
     */
    uint64_t bodySize() const
    {
        return _bodySize;
    }
    
    /**
     *  Body as a string
     *  @return string
     */
    std::string message() const
    {
        return std::string(_body, _bodySize);
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
