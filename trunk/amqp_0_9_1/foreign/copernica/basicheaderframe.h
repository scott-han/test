/**
 *  Class describing an AMQP basic header frame
 * 
 *  @copyright 2014 Copernica BV
 */

#include "headerframe.h"
#include "metadata.h"
#include "envelope.h"


// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_HEADER_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BASIC_HEADER_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class BasicHeaderFrame final : public HeaderFrame
{
private:
     /**
     *  Weight field, unused but must be sent, always value 0;
     *  @var uint16_t
     */
    uint16_t _weight = 0;

    /**
     *  Body size, sum of the sizes of all body frames following the content header
     *  @var uint64_t
     */
    uint64_t _bodySize;

    /**
     *  The meta data
     *  @var MetaData
     */
    MetaData _metadata;

protected:
    /**
     *  Encode a header frame to a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer &buffer) const override
    {
        // call base
        HeaderFrame::fill(buffer);

        // fill own fields.
        buffer.add(_weight);
        buffer.add(_bodySize);

        // the meta data
        _metadata.fill(buffer);
    }

public:
    /**
     *  Construct an empty basic header frame
     *
     *  All options are set using setter functions.
     * 
     *  @param  channel     channel we're working on
     *  @param  envelope    the envelope
     */
    BasicHeaderFrame(uint16_t channel, const Envelope &envelope) :
        HeaderFrame(channel, 10 + envelope.size()), // there are at least 10 bytes sent, weight (2), bodySize (8), plus the size of the meta data
        _bodySize(envelope.bodySize()),
        _metadata(envelope)
    {}

    /**
     *  Constructor to parse incoming frame
     *  @param  frame
     */
    BasicHeaderFrame(ReceivedFrame &frame) : 
        HeaderFrame(frame),
        _weight(frame.nextUint16()),
        _bodySize(frame.nextUint64()),
        _metadata(frame)
    {}
    
    /**
     *  Destructor
     */
    virtual ~BasicHeaderFrame() = default; 

    /**
     *  Size of the body
     *  @return uint64_t
     */
    uint64_t bodySize() const
    {
        return _bodySize;
    }

    /**
     *  The class ID
     *  @return uint16_t
     */
    virtual uint16_t classID() const override
    {
        return 60;
    }

		MetaData const & Get_metadata() const {
			return _metadata;
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
        if (!channel) return false;    
        
        // is there a current message?
        MessageImpl *message = channel->message();
        if (!message) return false;
        
        // store size
        message->setBodySize(_bodySize);
        
        // and copy the meta data
        message->set(_metadata);
        
        // for empty bodies we're ready now
        if (_bodySize == 0) channel->reportMessage();
        
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
