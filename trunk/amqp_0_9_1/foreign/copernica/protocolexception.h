/**
 *  ProtocolException.h
 *
 *  This exception is thrown internally in the library when invalid data is
 *  received from the server. The best remedy is to close the connection
 *
 *  @copyright 2014 Copernica BV
 */

// some contents modified by leon zadorin ...


#include "exception.h"

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_PROTOCOL_EXCEPTION_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_PROTOCOL_EXCEPTION_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class definition
 */
class ProtocolException : public Exception
{
public:
    /**
     *  Constructor
     *  @param  what
     */
    explicit ProtocolException(const std::string &what) : Exception(what) {}
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
