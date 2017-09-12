/**
 *  OutBuffer.h
 *
 *  This is a utility class for writing various data types to a binary
 *  string, and converting the values to network byte order
 *
 *  @copyright 2014 Copernica BV
 */

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_OUTBUFFER_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_OUTBUFFER_H

#include "endian.h"


// some contents modified by leon zadorin ...

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

class OutBuffer {
private:
    /**
     *  Pointer to the beginning of the buffer
     *  @var char*
     */
    char *_buffer;

    /**
     *  Pointer to the buffer to be filled
     *  @var char*
     */
    char *_current;

    /**
     *  Current size of the buffer
     *  @var size_t
     */
    size_t _size;

    /**
     *  The total capacity of the out buffer
     *  @var size_t
     */
    size_t _capacity;


public:

		OutBuffer(OutBuffer &&) = delete;
		OutBuffer(OutBuffer const &) = delete;
		OutBuffer& operator = (OutBuffer const &) = delete;

    OutBuffer(void * bfr, uint32_t capacity)
    {
        // initialize members
        _size = 0;
        _capacity = capacity;
        _buffer = _current = static_cast<char *>(bfr);
    }

		unsigned
		capacity() const 
		{
			return _capacity;
		}

    virtual ~OutBuffer() = default;

    /**
     *  Get access to the internal buffer
     *  @return const char*
     */
    const char *data() const
    {
        return _buffer;
    }

    /**
     *  Current size of the output buffer
     *  @return size_t
     */
    size_t size() const
    {
        return _size;
    }

    /**
     *  Add a binary buffer to the buffer
     *  @param  string  char* to the string
     *  @param  size    size of string
     */
    void add(const char *string, uint32_t size)
    {
        memcpy(_current, string, size);
        _current += size;
        _size += size;
    }

    /**
     *  Add a binary buffer to the buffer
     *  @param  string  char* to the string
     *  @param  size    size of string
     */
    void add(const std::string &string)
    {
        add(string.c_str(), string.size());
    }

    /**
     *  add a uint8_t to the buffer
     *  @param value    value to add
     */
    void add(uint8_t value)
    {
        memcpy(_current, &value, sizeof(value));
        _current += sizeof(value);
        _size += sizeof(value);
    }

    /**
     *  add a uint16_t to the buffer
     *  @param value    value to add
     */
    void add(uint16_t value)
    {
        uint16_t v = htobe16(value);
        memcpy(_current, &v, sizeof(v));
        _current += sizeof(v);
        _size += sizeof(v);
    }

    /**
     *  add a uint32_t to the buffer
     *  @param value    value to add
     */
    void add(uint32_t value)
    {
        uint32_t v = htobe32(value);
        memcpy(_current, &v, sizeof(v));
        _current += sizeof(v);
        _size += sizeof(v);
    }

    /**
     *  add a uint64_t to the buffer
     *  @param value    value to add
     */
    void add(uint64_t value)
    {
        uint64_t v = htobe64(value);
        memcpy(_current, &v, sizeof(v));
        _current += sizeof(v);
        _size += sizeof(v);
    }

    /**
     *  add a int8_t to the buffer
     *  @param value    value to add
     */
    void add(int8_t value)
    {
        memcpy(_current, &value, sizeof(value));
        _current += sizeof(value);
        _size += sizeof(value);
    }

    /**
     *  add a int16_t to the buffer
     *  @param value    value to add
     */
    void add(int16_t value)
    {
        int16_t v = htobe16(value);
        memcpy(_current, &v, sizeof(v));
        _current += sizeof(v);
        _size += sizeof(v);
    }

    /**
     *  add a int32_t to the buffer
     *  @param value    value to add
     */
    void add(int32_t value)
    {
        int32_t v = htobe32(value);
        memcpy(_current, &v, sizeof(v));
        _current += sizeof(v);
        _size += sizeof(v);
    }

    /**
     *  add a int64_t to the buffer
     *  @param value    value to add
     */
    void add(int64_t value)
    {
        int64_t v = htobe64(value);
        memcpy(_current, &v, sizeof(v));
        _current += sizeof(v);
        _size += sizeof(v);
    }

    /**
     *  add a float to the buffer
     *  @param value    value to add
     */
    void add(float value)
    {
        memcpy(_current, &value, sizeof(value));
        _current += sizeof(value);
        _size += sizeof(value);
    }

    /**
     *  add a double to the buffer
     *  @param value    value to add
     */
    void add(double value)
    {
        memcpy(_current, &value, sizeof(value));
        _current += sizeof(value);
        _size += sizeof(value);
    }
};


}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
