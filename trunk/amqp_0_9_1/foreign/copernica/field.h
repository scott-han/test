/**
 *  Available field types for AMQP
 * 
 *  @copyright 2014 Copernica BV
 */

#include <memory> 
#include "field_declaration.h" 
#include "array.h"
#include "table.h"
#include "receivedframe.h"
#include "booleanset.h"
#include "numericfield.h"
#include "decimalfield.h"
#include "stringfield.h"
#include "fieldproxy.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FIELD_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FIELD_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {


/**
 *  Decode a field by fetching a type and full field from a frame
 *  The returned field is allocated on the heap!
 *  @param  frame
 *  @return Field*
 */
Field * Decode_field(ReceivedFrame &frame)
{
    // get the type
    uint8_t type = frame.nextUint8();
    
    // create field based on type
    switch (type)
    {
        case 't':   return new BooleanSet(frame);
        case 'b':   return new Octet(frame);
        case 'B':   return new UOctet(frame);
        case 'U':   return new Short(frame);
        case 'u':   return new UShort(frame);
        case 'I':   return new Long(frame);
        case 'i':   return new ULong(frame);
        case 'L':   return new LongLong(frame);
        case 'l':   return new ULongLong(frame);
        case 'f':   return new Float(frame);
        case 'd':   return new Double(frame);
        case 'D':   return new DecimalField(frame);
        case 's':   return new ShortString(frame);
        case 'S':   return new LongString(frame);
        case 'A':   return new Array(frame);
        case 'T':   return new Timestamp(frame);
        case 'F':   return new Table(frame);
        default:    return nullptr;
    }
}

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif


