#include "core/ring-buffer.h"

void ring_buffer_setup(ring_buffer_t* rb, uint8_t* buffer, uint32_t size)
{
    rb->buffer = buffer;
    rb-> mask = size - 1; //this is a bitwise operation that is used to create a mask for the ring buffer. The mask is used to ensure that the read and write indices wrap around correctly when they reach the end of the buffer. By setting the mask to size - 1, it allows for efficient wrapping of the indices using bitwise AND operations.
    rb->read_index = 0;
    rb->write_index = 0;
}

bool ring_buffer_is_empty(ring_buffer_t* rb)
{
    return rb->read_index == rb->write_index;
}

bool ring_buffer_write(ring_buffer_t* rb, uint8_t byte)
{
    uint32_t local_write_index = rb->write_index; // Store the current write index in a local variable to avoid multiple accesses to the structure member, which can improve performance.
    uint32_t local_read_index = rb->read_index; // Store the current read index in a local variable to avoid multiple accesses to the structure member, which can improve performance.      

    uint32_t next_write_index = (local_write_index + 1) & rb->mask; //this is a bitwise operation that calculates the next write index for the ring buffer. It adds 1 to the current write index and then applies a bitwise AND operation with the mask. This ensures that the write index wraps around correctly when it reaches the end of the buffer, preventing overflow and allowing for efficient use of the buffer space.
    if (next_write_index == local_read_index) // Check if buffer is full
    {
        return false; // Buffer is full, cannot write
    }
    rb->buffer[local_write_index] = byte; // Write byte to buffer

    rb->write_index = next_write_index; // Update write index
    return true; // Write successful
}

bool ring_buffer_read(ring_buffer_t* rb, uint8_t* byte)
{
    uint32_t local_read_index = rb->read_index; // Store the current read index in a local variable to avoid multiple accesses to the structure member, which can improve performance.
    uint32_t local_write_index = rb->write_index; // Store the current write index in a local variable to avoid multiple accesses to the structure member, which can improve performance.
    if (local_read_index == local_write_index) // Check if buffer is empty
    {
        return false; // Buffer is empty, cannot read
    }
     *byte = rb->buffer[local_read_index]; // Read byte from buffer
    rb->read_index = (local_read_index + 1) & rb->mask; // Update read index
    return true; // Read successful
}
