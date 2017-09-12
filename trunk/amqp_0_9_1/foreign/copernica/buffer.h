/**
 *  Buffer.h
 *
 *  Interface that can be implemented by client applications and that
 *  is parsed to the Connection::parse() method.
 *
 *  Normally, the Connection::parse() method is fed with a byte
 *  array. However, if you're receiving big frames, it may be inconvenient
 *  to copy these big frames into continguous byte arrays, and you
 *  prefer using objects that internally use linked lists or other
 *  ways to store the bytes. In such sitations, you can implement this
 *  interface and pass that to the connection.
 *
 *  @author Emiel Bruijntjes <emiel.bruijntjes@copernica.com>
 *  @copyright 2014 Copernica BV
 */

// some contents modified by leon zadorin ...

#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BUFFER_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_BUFFER_H


#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace amqp_0_9_1 { namespace foreign { namespace copernica {

/**
 *  Class definition
 */
class Buffer
{
public:
    /**
     *  Total size of the buffer
     *  @return size_t
     */
    virtual size_t size() const = 0;

    /**
     *  Get access to a single byte
     * 
     *  No safety checks are necessary: this method will only be called
     *  for bytes that actually exist
     * 
     *  @param  pos         position in the buffer
     *  @return char        value of the byte in the buffer
     */
    virtual char byte(size_t pos) const = 0;
    
    /**
     *  Get access to the raw data
     *  @param  pos         position in the buffer
     *  @param  size        number of continuous bytes
     *  @return char*
     */
    virtual const char *data(size_t pos, size_t size) const = 0;
    
    /**
     *  Copy bytes to a buffer
     * 
     *  No safety checks are necessary: this method will only be called
     *  for bytes that actually exist
     * 
     *  @param  pos         position in the buffer
     *  @param  size        number of bytes to copy
     *  @param  buffer      buffer to copy into
     *  @return void*       pointer to buffer
     */
    virtual void *copy(size_t pos, size_t size, void *buffer) const = 0;

};

}}}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
