
/**
 *  AMQP field array
 *
 *  @copyright 2014 Copernica BV
 */

/**
 *  Set up namespace
 */

#include "field_declaration.h"
#include "fieldproxy.h"
#include "receivedframe.h"

// some contents modified by leon zadorin ...
#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_ARRAY_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_ARRAY_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  AMQP field array
 */
class Array final : public Field
{
private:
    /**
     *  Definition of an array as a vector
     *  @typedef
     */
    typedef std::vector< ::std::shared_ptr<Field> > FieldArray;

    /**
     *  The actual fields
     *  @var FieldArray
     */
    FieldArray _fields;

public:
    /**
     *  Constructor to construct an array from a received frame
     *
     *  @param  frame   received frame
     */
		Array(ReceivedFrame &frame)
		{
				// use this to see if we've read too many bytes.
				uint32_t charsToRead = frame.nextUint32();

				// keep going until all data is read
				while (charsToRead > 0)
				{
						// one byte less for the field type
						charsToRead -= 1;

						// read the field type and construct the field
						Field *field = copernica::Decode_field(frame);
						if (!field) continue;

						// less bytes to read
						charsToRead -= field->size();

						// add the additional field
						_fields.push_back(std::shared_ptr<Field>(field));
				}
		}

    /**
     *  Copy constructor
     *  @param  array
     */
    Array(const Array &array)
		{
				// loop through the other array
				for (auto iter = array._fields.begin(); iter != array._fields.end(); iter++)
				{
						// add to this vector
						_fields.push_back(std::shared_ptr<Field>((*iter)->clone()));
				}
		}

    /**
     *  Move constructor
     *  @param  array
     */
    //Array(Array &&array) : _fields(std::move(array._fields)) {}
    Array(Array &&) = default;

    /**
     *  Constructor for an empty Array
     */
    Array() = default; 

    /**
     * Destructor
     */
    virtual ~Array() = default; 

    /**
     *  Create a new instance of this object
     *  @return Field*
     */
    virtual std::shared_ptr<Field> clone() const override
    {
        return std::make_shared<Array>(*this);
    }

    /**
     *  Get the size this field will take when
     *  encoded in the AMQP wire-frame format
     *  @return size_t
     */
    virtual size_t size() const override
		{
				// store the size (four bytes for the initial size)
				size_t size = 4;

				// iterate over all elements
				for (auto item : _fields)
				{
						// add the size of the field type and size of element
						size += sizeof(item->typeID());
						size += item->size();
				}

				// return the result
				return size;
		}

    /**
     *  Set a field
     *
     *  @param  index   field index
     *  @param  value   field value
     *  @return Array
     */
    Array set(uint8_t index, const Field &value)
    {
        // construct a shared pointer
        auto ptr = value.clone();

        // should we overwrite an existing record?
        if (index >= _fields.size())
        {
            // append index
            _fields.push_back(ptr);
        }
        else
        {
            // overwrite pointer
            _fields[index] = ptr;
        }

        // allow chaining
        return *this;
    }

    /**
     *  Get a field
     *
     *  If the field does not exist, an empty string is returned
     *
     *  @param  index   field index
     *  @return Field
     */
    const Field &get(uint8_t index) const
		{
				// check whether we have that many elements
				if (index >= _fields.size())
					throw ProtocolException("current implementation requires to know that index exists in array");

				// get value
				return *_fields[index];
		}

    /**
     *  Get number of elements on this array
     *
     *  @return array size
     */
    uint32_t count() const
		{
			return _fields.size();
		}

    /**
     *  Remove last element from array
     */
    void pop_back()
		{
				_fields.pop_back();
		}

    /**
     *  Add field to end of array
     *
     *  @param value
     */
    void push_back(const Field &value)
		{
				_fields.push_back(std::shared_ptr<Field>(value.clone()));
		}

    /**
     *  Get a field
     *
     *  @param  index   field index
     *  @return ArrayFieldProxy
     */
    ArrayFieldProxy operator[](uint8_t index)
    {
        return ArrayFieldProxy(this, index);
    }
    
    /**
     *  Get a const field
     *  @param  index   field index
     *  @return Field
     */
    const Field &operator[](uint8_t index) const
    {
        return get(index);
    }

    /**
     *  Write encoded payload to the given buffer.
     *  @param  buffer
     */
    virtual void fill(OutBuffer& buffer) const override
		{
				// store total size for all elements
				buffer.add(static_cast<uint32_t>(size()-4));

				// iterate over all elements
				for (auto item : _fields)
				{
						// encode the element type and element
						buffer.add((uint8_t)item->typeID());
						item->fill(buffer);
				}
		}

    /**
     *  Get the type ID that is used to identify this type of
     *  field in a field table
     *  @return char
     */
    virtual char typeID() const override
    {
        return 'A';
    }

    /**
     *  Output the object to a stream
     *  @param std::ostream
     */
    void output(std::ostream &stream) const override
    {
        // prefix
        stream << "array(";
        
        // is this the first iteration
        bool first = true;
        
        // loop through all members
        for (auto &iter : _fields) 
        {
            // split with comma
            if (!first) stream << ",";
            
            // show output
            stream << *iter;
            
            // no longer first iter
            first = false;
        }
        
        // postfix
        stream << ")";
    }
    
    /**
     *  Cast to array
     *  @return Array
     */
    Array const & Convert_to_array () const override
    {
        // this already is an array, so no cast is necessary
        return *this;
    }
};


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif
#include "field.h"
#endif

