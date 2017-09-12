/**
 *  AMQP field table
 * 
 *  @copyright 2014 Copernica BV
 */

#include <map>


#include "field_declaration.h"
#include "fieldproxy.h"
#include "receivedframe.h"


// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_TABLE_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_TABLE_H
#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  AMQP field table
 */
class Table final : public Field
{
private:
    /**
     *  We define a custom type for storing fields
     *  @typedef    FieldMap
     */
    typedef std::map<std::string, std::shared_ptr<Field> > FieldMap;

    /**
     *  Store the fields
     *  @var    FieldMap
     */
    FieldMap _fields;

public:
    /**
     *  Constructor that creates an empty table
     */
    Table() = default;

    /**
     *  Decode the data from a received frame into a table
     *
     *  @param  frame   received frame to decode
     */
    Table(ReceivedFrame &frame)
		{
				// table buffer begins with the number of bytes to read 
				uint32_t bytesToRead = frame.nextUint32();
				
				// keep going until the correct number of bytes is read. 
				while (bytesToRead > 0)
				{
						// field name and type
						ShortString name(frame);
						
						// subtract number of bytes to read, plus one byte for the decoded type
						bytesToRead -= (name.size() + 1);
						
						// get the field
						Field *field = copernica::Decode_field(frame);
						if (!field) continue;
						
						// add field
						_fields[name] = std::shared_ptr<Field>(field);
						
						// subtract size
						bytesToRead -= field->size();
				}
		}
    
    /**
     *  Copy constructor
     *  @param  table
     */
    Table(const Table &table)
		{
				// loop through the table records
				for (auto iter = table._fields.begin(); iter != table._fields.end(); iter++)
				{
						// add the field
						_fields[iter->first] = std::shared_ptr<Field>(iter->second->clone());
				}
		}

    /**
     *  Move constructor
     *  @param  table
     */
    //Table(Table &&table) : _fields(std::move(table._fields)) {}
    Table(Table &&) = default;

    /**
     *  Destructor
     */
    virtual ~Table() = default;

    /**
     *  Assignment operator
     *  @param  table
     *  @return Table
     */
    Table &operator=(const Table &table)
		{
				// skip self assignment
				if (this == &table) return *this;
				
				// empty current fields
				_fields.clear();
				
				// loop through the table records
				for (auto iter = table._fields.begin(); iter != table._fields.end(); iter++)
				{
						// add the field
						_fields[iter->first] = std::shared_ptr<Field>(iter->second->clone());
				}
				
				// done
				return *this;
		}
    
    /**
     *  Move assignment operator
     *  @param  table
     *  @return Table
     */
    Table &operator=(Table &&table)
		{
				// skip self assignment
				if (this == &table) return *this;
				
				// copy fields
				_fields = std::move(table._fields);
				
				// done
				return *this;
		}
    

    /**
     *  Create a new instance on the heap of this object, identical to the object passed
     *  @return Field*
     */
    virtual std::shared_ptr<Field> clone() const override
    {
        return std::make_shared<Table>(*this);
    }

    /**
     *  Get the size this field will take when
     *  encoded in the AMQP wire-frame format
     */
    virtual size_t size() const override
		{
				// add the size of the uint32_t indicating the size
				size_t size = 4;

				// iterate over all elements
				for (auto iter(_fields.begin()); iter != _fields.end(); ++iter)
				{
						// get the size of the field name
						ShortString name(iter->first);
						size += name.size();

						// add the size of the field type
						size += sizeof(iter->second->typeID());

						// add size of element to the total
						size += iter->second->size();
				}

				// return the result
				return size;
		}

    /**
     *  Set a field
     *
     *  @param  name    field name
     *  @param  value   field value
     */
    Table set(const std::string& name, const Field &value)
    {
        // copy to a new pointer and store it
        _fields[name] = value.clone();

        // allow chaining
        return *this;
    }

    /**
     *  Get a field
     * 
     *  If the field does not exist, an empty string field is returned
     *
     *  @param  name    field name
     *  @return         the field value
     */
    const Field &get(const std::string &name) const
		{
				// locate the element first
				auto iter(_fields.find(name));

				// check whether the field was found
				if (iter == _fields.end()) 
					throw ProtocolException("current implementation requires to know that field exists in table");

				// done
				return *iter->second;
		}

	 FieldMap const & get_fields() const
	 {
		 return _fields;
	 }

    /**
     *  Get a field
     *
     *  @param  name    field name
     */
    AssociativeFieldProxy operator[](const std::string& name)
    {
        return AssociativeFieldProxy(this, name);
    }

    /**
     *  Get a field
     *
     *  @param  name    field name
     */
    AssociativeFieldProxy operator[](const char *name)
    {
        return AssociativeFieldProxy(this, name);
    }

    /**
     *  Get a const field
     *
     *  @param  name    field name
     */
    const Field &operator[](const std::string& name) const
    {
        return get(name);
    }

    /**
     *  Get a const field
     *
     *  @param  name    field name
     */
    const Field &operator[](const char *name) const
    {
        return get(name);
    }

    /**
     *  Write encoded payload to the given buffer. 
     *  @param  buffer
     */
    virtual void fill(OutBuffer& buffer) const override
		{
				// add size
				buffer.add(static_cast<uint32_t>(size()-4));

				// loop through the fields
				for (auto iter(_fields.begin()); iter != _fields.end(); ++iter)
				{
						// encode the field name
						ShortString name(iter->first);
						name.fill(buffer);
						
						// encode the element type
						buffer.add((uint8_t) iter->second->typeID());

						// encode element
						iter->second->fill(buffer);
				}
		}

    /**
     *  Get the type ID that is used to identify this type of
     *  field in a field table
     */
    virtual char typeID() const override
    {
        return 'F';
    }

    /**
     *  Output the object to a stream
     *  @param std::ostream
     */
    void output(std::ostream &stream) const override
    {
        // prefix
        stream << "table(";
        
        // is this the first iteration
        bool first = true;
        
        // loop through all members
        for (auto &iter : _fields) 
        {
            // split with comma
            if (!first) stream << ",";
            
            // show output
            stream << iter.first << ":" << *iter.second;
            
            // no longer first iter
            first = false;
        }
        
        // postfix
        stream << ")";
    }

    /**
     *  Cast to table
     *  @return Table
     */
    Table const & Convert_to_table () const override
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
