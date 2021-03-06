/**
 *  Class describing initial connection setup acknowledge frame
 * 
 *  @copyright 2014 Copernica BV
 */

#include "connectionframe.h"
#include "table.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONSTARTOK_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_CONNECTIONSTARTOK_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class implementation
 */
class ConnectionStartOKFrame final : public ConnectionFrame
{
private:
    /**
     *  Additional client properties
     *  @note:  exact properties are not specified
     *          and are implementation-dependent
     *  @var Table
     */
    Table _properties;

    /**
     *  The selected security mechanism
     *  @var ShortString
     */
    ShortString _mechanism;

    /**
     *  The security response
     *  @var LongString
     */
    LongString _response;

    /**
     *  The selected locale
     *  @var ShortString
     */
    ShortString _locale;

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

        // add fields
        _properties.fill(buffer);
        _mechanism.fill(buffer);
        _response.fill(buffer);
        _locale.fill(buffer);
    }

public:
    /**
     *  Construct a connection start ok frame from a received frame
     *
     *  @param  frame   received frame
     */
    ConnectionStartOKFrame(ReceivedFrame &frame) :
        ConnectionFrame(frame),
        _properties(frame),
        _mechanism(frame),
        _response(frame),
        _locale(frame)
    {}

    /** 
     *  Construct a connection start ok frame
     *
     *  @param  properties  client propertes
     *  @param  mechanism   selected security mechanism
     *  @param  response    security response data
     *  @param  locale      selected locale.
     */
    ConnectionStartOKFrame(const Table& properties, const std::string& mechanism, const std::string& response, const std::string& locale) :
        ConnectionFrame((properties.size() + mechanism.length() + response.length() + locale.length() + 6)), // 1 byte extra per shortstring, 4 per longstring
        _properties(properties),
        _mechanism(mechanism),
        _response(response),
        _locale(locale)
    {}

    /**
     *  Destructor
     */
    virtual ~ConnectionStartOKFrame() = default;

    /**
     *  Method id
     */
    virtual uint16_t methodID() const override
    {
        return 11;
    }

    /**
     *  Additional client properties
     *  @note:  exact properties are not specified
     *          and are implementation-dependent
     *  @return Table
     */
    const Table& properties() const
    {
        return _properties;
    }

    /**
     *  The selected security mechanism
     *  @return string
     */
    const std::string& mechanism() const
    {
        return _mechanism;
    }

    /**
     *  The security response
     *  @return string
     */
    const std::string& response() const
    {
        return _response.value();
    }

    /**
     *  The selected locale
     *  @return string
     */
    const std::string & locale() const
    {
        return _locale;
    }

    /**
     *  Is this a frame that is part of the connection setup?
     *  @return bool
     */
    virtual bool partOfHandshake() const override
    {
        return true;
    }

};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
