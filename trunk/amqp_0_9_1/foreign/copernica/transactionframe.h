/**
 *  Class describing an AMQP transaction frame
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
class TransactionFrame : public MethodFrame
{
protected:
    /**
     *  Constructor
     *  @param  channel     channel identifier
     *  @param  size        frame size
     */
    TransactionFrame(uint16_t channel, uint32_t size) :
        MethodFrame(channel, size)
    {}

    /**
     *  Constructor based on incoming frame
     *  @param  frame
     */
    TransactionFrame(ReceivedFrame &frame) :
        MethodFrame(frame)
    {}

public:
    /**
     *  Destructor
     */
    virtual ~TransactionFrame() = default;

    /**
     *  Class id
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 90;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


