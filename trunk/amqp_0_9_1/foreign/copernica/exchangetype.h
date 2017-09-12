#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCHANGETYPE_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_EXCHANGETYPE_H
/**
 *  ExchangeType.h
 *
 *  The various exchange types that are supported
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
 *  The class
 */
enum ExchangeType
{
    fanout,
    direct,
    topic,
    headers
};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
