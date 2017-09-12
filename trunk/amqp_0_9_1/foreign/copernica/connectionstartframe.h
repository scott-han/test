/**
 *  Class describing initial connection setup
 * 
 *  This frame is sent by the server to the client, right after the connection
 *  is opened. It contains the initial connection properties, and the protocol
 *  number.
 * 
 *  @copyright 2014 Copernica BV
 */

#include "connectionframe.h"
#include "table.h"

// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONSTART_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONSTART_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ConnectionStartFrame final : public ConnectionFrame
{
private:
    /**
     *  Major AMQP version number
     *  @var uint8_t
     */
    uint8_t _major;

    /**
     *  Minor AMQP version number
     *  @var uint8_t
     */
    uint8_t _minor;

    /**
     *  Additional server properties
     *  @note:  exact properties are not specified
     *          and are implementation-dependent
     *  @var Table
     */
    Table _properties;

    /**
     *  Available security mechanisms
     *  @var LongString
     */
    LongString _mechanisms;

    /**
     *  Available message locales
     *  @var LongString
     */
    LongString _locales;

protected:
    /**
     *  Encode a frame on a string buffer
     *
     *  @param  buffer  buffer to write frame to
     */
    virtual void fill(OutBuffer& buffer) const override
    {
        // call base
        ConnectionFrame::fill(buffer);

        // encode all fields
        buffer.add(_major);
        buffer.add(_minor);
        _properties.fill(buffer);
        _mechanisms.fill(buffer);
        _locales.fill(buffer);
    }

public:
    /**
     *  Client-side constructer for a connection start frame
     *
     *  @param  major       major protocol version
     *  @param  minor       minor protocol version
     *  @param  properties  server properties
     *  @param  mechanisms  available security mechanisms
     *  @param  locales     available locales
     */
    ConnectionStartFrame(uint8_t major, uint8_t minor, const Table& properties, const std::string& mechanisms, const std::string& locales) :
        ConnectionFrame((properties.size() + mechanisms.length() + locales.length() + 10)), // 4 for each longstring (size-uint32), 2 major/minor
        _major(major),
        _minor(minor),
        _properties(properties),
        _mechanisms(mechanisms),
        _locales(locales)
    {}

    /**
     *  Construct a connection start frame from a received frame
     *
     *  @param  frame   received frame
     */
    ConnectionStartFrame(ReceivedFrame &frame) :
        ConnectionFrame(frame),
        _major(frame.nextUint8()),
        _minor(frame.nextUint8()),
        _properties(frame),
        _mechanisms(frame),
        _locales(frame)
    {}

    /**
     *  Destructor
     */
    virtual ~ConnectionStartFrame() = default;

    /**
     *  Method id
     *  @return uint16_t
     */
    virtual uint16_t methodID() const override
    {
        return 10;
    }

    /**
     *  Major AMQP version number
     *  @return uint8_t
     */
    uint8_t major() const
    {
        return _major;
    }

    /**
     *  Minor AMQP version number
     *  @return uint8_t
     */
    uint8_t minor() const
    {
        return _minor;
    }

    /**
     *  Additional server properties
     *  @note:  exact properties are not specified
     *          and are implementation-dependent
     * 
     *  @return Table
     */
    const Table& properties() const
    {
        return _properties;
    }

    /**
     *  Available security mechanisms
     *  @return string
     */
    const std::string &mechanisms() const
    {
        return _mechanisms;
    }

    /**
     *  Available message locales
     *  @return string
     */
    const std::string &locales() const
    {
        return _locales;
    }
    
    /**
     *  Process the connection start frame
     *  @param  connection
     *  @return bool
     *  @internal
     */
    virtual bool process(ConnectionImpl *) override
    {
        return true;
    }
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
