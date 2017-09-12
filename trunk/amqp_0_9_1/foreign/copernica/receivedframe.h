/**
 *  ReceivedFrame.h
 *
 *  The received frame class is a wrapper around a data buffer, it tries to 
 *  find out if the buffer is big enough to contain an entire frame, and
 *  it will try to recognize the frame type in the buffer
 *
 *  This is a class that is used internally by the AMQP library. As a used
 *  of this library, you normally do not have to instantiate it.
 *
 *  @documentation public
 */

// some contents modified by leon zadorin ...

#include "buffer.h"

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_RECEIVED_FRAME_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_RECEIVED_FRAME_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {
	class ConnectionImpl;

/**
 *  Class definition
 */
class ReceivedFrame
{
private:
    /**
     *  The buffer we are reading from
     *  @var    Buffer
     */
    const Buffer &_buffer;

    /**
     *  Number of bytes already processed
     *  @var    uint32_t
     */
    uint32_t _skip = 0;

    /**
     *  Type of frame
     *  @var    uint8_t
     */
    uint8_t _type = 0;

    /**
     *  Channel identifier
     *  @var    uint16_t
     */
    uint16_t _channel = 0;

    /**
     *  The payload size
     *  @var    uint32_t
     */
    uint32_t _payloadSize = 0;

		// hack
public:
		uint16_t classID;
		uint16_t methodID;
		uint8_t type() const
		{
			return _type;
		}
private:

	class FrameCheck
	{
	private:
			/**
			 *  The frame
			 *  @var ReceivedFrame
			 */
			ReceivedFrame *_frame;
			
			/**
			 *  The size that is checked
			 *  @var size_t
			 */
			size_t _size;
			
	public:
			/**
			 *  Constructor
			 *  @param  frame
			 *  @param  size
			 */
			FrameCheck(ReceivedFrame *frame, size_t size) : _frame(frame), _size(size)
			{
					// no problem is there are still enough bytes left
					if (frame->_buffer.size() - frame->_skip >= size) return;
					
					// frame buffer is too small
					throw ProtocolException("frame out of range");
			}
			
			~FrameCheck()
			{
					// update the number of bytes to skip
					_frame->_skip += _size;
			}
	};

public:
    /**
     *  Constructor
     *  @param  buffer      Binary buffer
     *  @param  max         Max buffer size
     */
    ReceivedFrame(const Buffer &buffer, uint32_t max) : _buffer(buffer)
		{
				// we need enough room for type, channel, the payload size and the end-of-frame byte
				if (buffer.size() < 8) 
					throw ProtocolException("frame must be over 7 bytes");

				// get the information
				_type = nextUint8();
				_channel = nextUint16();
				_payloadSize = nextUint32();

				if (max > 0 && _payloadSize > max - 8) 
					throw ProtocolException("frame size exceeded");

				// check if the buffer is big enough to contain all data
				if (buffer.size() >= _payloadSize + 8) {
					if ((uint8_t)buffer.byte(_payloadSize+7) != 0xce) 
						throw ProtocolException("invalid end of frame marker");
				} else
					throw ProtocolException("frame was supposed to be complete by now");

		}

    /**
     *  Return the channel identifier
     *  @return uint16_t
     */
    uint16_t channel() const
    {
        return _channel;
    }

    /**
     *  Total size of the frame (headers + payload)
     *  @return uint32_t
     */
    uint64_t totalSize() const
    {
        // payload size + size of headers and end of frame byte
        return _payloadSize + 8;
    }

    /**
     *  The size of the payload
     *  @return uint32_t
     */
    uint32_t payloadSize() const
    {
        return _payloadSize;
    }

    /**
     *  Read the next uint8_t from the buffer
     *  
     *  @return uint8_t         value read
     */
    uint8_t nextUint8()
		{
				// check if there is enough size
				FrameCheck check(this, 1);
				
				// get a byte
				return _buffer.byte(_skip);
		}

    /**
     *  Read the next int8_t from the buffer
     *  
     *  @return int8_t          value read
     */
    int8_t nextInt8()
		{
				// check if there is enough size
				FrameCheck check(this, 1);
				
				// get a byte
				return (int8_t)_buffer.byte(_skip);
		}

    /**
     *  Read the next uint16_t from the buffer
     *  
     *  @return uint16_t        value read
     */
    uint16_t nextUint16()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(uint16_t));

				// get two bytes, and convert to host-byte-order
				uint16_t value;
				_buffer.copy(_skip, sizeof(uint16_t), &value);
				return be16toh(value);
		}

    /**
     *  Read the next int16_t from the buffer
     *  
     *  @return int16_t     value read
     */
    int16_t nextInt16()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(int16_t));
				
				// get two bytes, and convert to host-byte-order
				int16_t value;
				_buffer.copy(_skip, sizeof(int16_t), &value);
				return be16toh(value);
		}

    /**
     *  Read the next uint32_t from the buffer
     *  
     *  @return uint32_t        value read
     */
    uint32_t nextUint32()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(uint32_t));
				
				// get four bytes, and convert to host-byte-order
				uint32_t value;
				_buffer.copy(_skip, sizeof(uint32_t), &value);
				return be32toh(value);
		}

    /**
     *  Read the next int32_t from the buffer
     *  
     *  @return int32_t     value read
     */
    int32_t nextInt32()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(int32_t));
				
				// get four bytes, and convert to host-byte-order
				int32_t value;
				_buffer.copy(_skip, sizeof(int32_t), &value);
				return be32toh(value);
		}

    /**
     *  Read the next uint64_t from the buffer
     *  
     *  @return uint64_t        value read
     */
    uint64_t nextUint64()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(uint64_t));
				
				// get eight bytes, and convert to host-byte-order
				uint64_t value;
				_buffer.copy(_skip, sizeof(uint64_t), &value);
				return be64toh(value);
		}

    /**
     *  Read the next int64_t from the buffer
     *  
     *  @return int64_t     value read
     */
    int64_t nextInt64()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(int64_t));
				
				// get eight bytes, and convert to host-byte-order
				int64_t value;
				_buffer.copy(_skip, sizeof(int64_t), &value);
				return be64toh(value);
		}

    /**
     *  Read a float from the buffer
     *
     *  @return float       float read from buffer. 
     */
    float nextFloat()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(float));
				
				// get four bytes
				float value;
				_buffer.copy(_skip, sizeof(float), &value);
				return value;
		}

    /**
     *  Read a double from the buffer
     *
     *  @return double      double read from buffer
     */
    double nextDouble()
		{
				// check if there is enough size
				FrameCheck check(this, sizeof(double));
				
				// get eight bytes, and convert to host-byte-order
				double value;
				_buffer.copy(_skip, sizeof(double), &value);
				return value;
		}

    /**
     *  Get a pointer to the next binary buffer of a certain size
     *  @param  size
     *  @return char*
     */
    const char *nextData(uint32_t size)
		{
				// check if there is enough size
				FrameCheck check(this, size);
				
				// get the data
				return _buffer.data(_skip, size);
		}

    /**
     *  Process the received frame
     * 
     *  If this method returns false, it means that the frame was not processed,
     *  because it was an unrecognized frame. This does not mean that the 
     *  connection is now in an invalid state however.
     * 
     *  @param  connection  the connection over which the data was received
     *  @return bool        was the frame fully processed
     *  @internal
     */
    bool process(ConnectionImpl *)
		{
			return false;
		}

    /**
     *  The checker may access private data
     */
    friend class FrameCheck;

		void
		load_class_id()
		{

			classID = nextUint16();

		}
		void
		load_method_id()
		{

			methodID = nextUint16();

		}

};
}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif

