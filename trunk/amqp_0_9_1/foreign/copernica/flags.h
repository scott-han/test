#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FLAGS_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FLAGS_H
/**
 *  AmqpFlags.h
 *
 *  The various flags that are supported
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
 *  All bit flags
 *  @var int
 */
extern const int durable;
extern const int autodelete;
extern const int active;
extern const int passive;
extern const int ifunused;
extern const int ifempty;
extern const int global;
extern const int nolocal;
extern const int noack;
extern const int exclusive;
extern const int mandatory;
extern const int immediate;
extern const int redelivered;
extern const int multiple;
extern const int requeue;

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
