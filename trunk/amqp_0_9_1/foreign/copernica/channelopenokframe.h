/**
 *  Class describing a channel open acknowledgement frame
 * 
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNELOPENOK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CHANNELOPENOK_FRAME_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ChannelOpenOKFrame final : public ChannelFrame
{
protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        ChannelFrame::fill(buffer);
        
        // create and encode the silly deprecated argument
        LongString unused;
        
        // add to the buffer
        unused.fill(buffer);
    }
    
public:
    /**
     *  Constructor based on client information
     *  @param  channel     Channel identifier
     */
    ChannelOpenOKFrame(uint16_t channel) : ChannelFrame(channel, 4) {} // 4 for the longstring size value

    /**
     *  Constructor based on incoming frame
     *  @param  frame
     */
    ChannelOpenOKFrame(ReceivedFrame &frame) : ChannelFrame(frame) 
    {
        // read in a deprecated argument
        LongString unused(frame);
    }
    
    /**
     *  Destructor
     */
    virtual ~ChannelOpenOKFrame() = default;

    /**
     *  Method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 11;
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
        if(!channel) return false;    
        
        // report that the channel is open
        channel->reportReady();
        
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
