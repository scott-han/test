/**
 *  Class describing an AMQP transaction select frame
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
class TransactionSelectFrame final : public TransactionFrame
{
public:
    /**
     *  Decode a transaction select frame from a received frame
     *
     *  @param   frame   received frame to decode
     */
    TransactionSelectFrame(ReceivedFrame& frame) :
        TransactionFrame(frame)
    {}

    /**
     *  Construct a transaction select frame
     * 
     *  @param   channel     channel identifier
     *  @return  newly created transaction select frame
     */
    TransactionSelectFrame(uint16_t channel) :
        TransactionFrame(channel, 0)
    {}

    /**
     *  Destructor
     */
    virtual ~TransactionSelectFrame() = default;

    /**
     * return the method id
     * @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 10;
    }    
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


