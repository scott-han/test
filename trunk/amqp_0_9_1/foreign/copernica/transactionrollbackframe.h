/**
 *  Class describing an AMQP transaction rollback frame
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
class TransactionRollbackFrame final : public TransactionFrame
{
public:
    /**
     *  Destructor
     */
    virtual ~TransactionRollbackFrame() = default;

    /**
     *  Decode a transaction rollback frame from a received frame
     *
     *  @param   frame   received frame to decode
     */
    TransactionRollbackFrame(ReceivedFrame& frame) :
        TransactionFrame(frame)
    {}

    /**
     *  Construct a transaction rollback frame
     * 
     *  @param   channel     channel identifier
     *  @return  newly created transaction rollback frame
     */
    TransactionRollbackFrame(uint16_t channel) :
        TransactionFrame(channel, 0)
    {}

    /**
     *  return the method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 30;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


