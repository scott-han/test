/**
 *  Available field types for AMQP
 * 
 *  @copyright 2014 Copernica BV
 */

#include <memory> 

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FIELD_DECLARATION_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_FIELD_DECLARATION_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

class ReceivedFrame;
class Array;
class Table;
class OutBuffer;

/**
 *  Base field class
 *
 *  This class cannot be constructed, but serves
 *  as the base class for all AMQP field types
 */
class Field
{
public:
    /**
     *  Destructor
     */
    virtual ~Field() = default;

    /**
     *  Create a new instance on the heap of this object, identical to the object passed
     *  @return Field*
     */
    virtual std::shared_ptr<Field> clone() const = 0;

    /**
     *  Get the size this field will take when
     *  encoded in the AMQP wire-frame format
     *  @return size_t
     */
    virtual size_t size() const = 0;

    /**
     *  Write encoded payload to the given buffer.
     *  @param  buffer
     */
    virtual void fill(OutBuffer& buffer) const = 0;

    /**
     *  Get the type ID that is used to identify this type of
     *  field in a field table
     *  @return char
     */
    virtual char typeID() const = 0;
    
    /**
     *  Output the object to a stream
     *  @param std::ostream
     */
    virtual void output(std::ostream &stream) const = 0;
    
    /**
     *  Casting operators
     *  @return mixed
     */
    virtual operator const std::string& () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator const char * () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator uint8_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator uint16_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator uint32_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator uint64_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator int8_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator int16_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator int32_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator int64_t () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator float () const { throw ProtocolException("field type cannot be cast to target type"); }
    virtual operator double () const { throw ProtocolException("field type cannot be cast to target type"); }

		// These are exceptions because of C++ standard (12.3.2): "A conversion function is never used to convert ... object ... to the same object type" -- so overriding this in Array class with 'convert to self op' will not be playing nice w.r.t. standard. Clang/llvm picks these up and warns also... so instead will not use 'conversion function' but rather expliti method... 
    operator const Array& () const { return Convert_to_array(); }
		virtual Array const & Convert_to_array() const {
			throw ProtocolException("field type cannot be cast to target type"); 
		}
    operator const Table& () const { return Convert_to_table(); }
		virtual Table const & Convert_to_table() const {
			throw ProtocolException("field type cannot be cast to target type"); 
		}
};
/**
 *  Custom output stream operator
 *  @param  stream
 *  @param  field
 *  @return ostream
 */
	inline ::std::ostream &operator<<(::std::ostream &stream, const Field &field)
{
    field.output(stream);
    return stream;
}

inline static Field * Decode_field(ReceivedFrame &frame);

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#endif
