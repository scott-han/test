/**
 *  Frame.h
 *
 *  Base class for frames. This base class can not be constructed from outside
 *  the library, and is only used internally.
 *
 *  @copyright 2014 Copernica BV
 */


#include "protocolexception.h"

// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FRAME_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

	class ConnectionImpl;

/**
 *  Class definition
 */
class Frame
{
protected:
    /**
     *  Protected constructor to ensure that no objects are created from
     *  outside the library
     */
    Frame() {}

public:
    /**
     *  Destructor
     */
    virtual ~Frame() = default;

    /**
     *  return the total size of the frame
     *  @return uint32_t
     */
    virtual uint32_t totalSize() const = 0;

    /**
     *  Fill an output buffer
     *  @param  buffer
     */
    virtual void fill(OutBuffer &buffer) const = 0;

    /**
     *  Is this a frame that is part of the connection setup?
     *  @return bool
     */
    virtual bool partOfHandshake() const { return false; }

    /**
     *  Is this a frame that is part of the connection close operation?
     *  @return bool
     */
    virtual bool partOfShutdown() const { return false; }

    /**
     *  Does this frame need an end-of-frame seperator?
     *  @return bool
     */
    virtual bool needsSeparator() const { return true; }

    /**
     *  Is this a synchronous frame?
     *
     *  After a synchronous frame no more frames may be
     *  sent until the accompanying -ok frame arrives
     */
    virtual bool synchronous() const { return false; }

		template <typename OutBuffer>
    void 
		to_wire(OutBuffer && bfr) const
    {
				if (totalSize() > bfr.capacity())
					throw ::std::runtime_error("amqp-0.9.1 frame too large");
        fill(bfr);
        if (needsSeparator()) 
					bfr.add((uint8_t)206);
    }

    /**
     *  Process the frame
     *  @param  connection      The connection over which it was received
     *  @return bool            Was it succesfully processed?
     */
    virtual bool process(ConnectionImpl *)
    {
        // this is an exception
        throw ProtocolException("unimplemented frame");

        // unreachable
        return false;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
