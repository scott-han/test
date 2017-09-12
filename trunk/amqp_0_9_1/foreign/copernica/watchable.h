#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_WATCHABLE_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_WATCHABLE_H
/**
 *  Watchable.h
 *
 *  Every class that overrides from the Watchable class can be monitored for
 *  destruction by a Monitor object
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
 *  Class definition
 */
class Watchable
{
private:
    /**
     *  The monitors
     *  @var set
     */
    std::set<Monitor*> _monitors;

    /**
     *  Add a monitor
     *  @param  monitor
     */
    void add(Monitor *monitor)
    {
        _monitors.insert(monitor);
    }
    
    /**
     *  Remove a monitor
     *  @param  monitor
     */
    void remove(Monitor *monitor)
    {
        _monitors.erase(monitor);
    }

public:
    /**
     *  Destructor
     */
    virtual ~Watchable();
    
    /**
     *  Only a monitor has full access
     */
    friend class Monitor;
};     

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
