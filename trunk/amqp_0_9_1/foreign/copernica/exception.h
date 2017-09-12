/**
 *  Exception.h
 *
 *  Base class for all AMQP exceptions
 *
 *  @copyright 2014 Copernica BV
 */


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCEPTION_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCEPTION_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Base exception class
 */
class Exception : public std::runtime_error
{
protected:
    /**
     *  Constructor
     *  @param  what
     */
    explicit Exception(const std::string &what) : runtime_error(what) {}
};
    
}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif


#endif
